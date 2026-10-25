#include "updater.h"

#include <version.h>
#include <features/config/config.h>
#include <settings.h>
#include <json/json.hpp>

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "version.lib")

namespace
{
	// The programs natware keeps up to date. natware's own tags have no prefix, so the natware
	// versions released before the split still find their updates.
	struct spec_t { const char* name; const char* file; const char* prefix; bool self; };
	constexpr spec_t kSpecs[] = {
		{ "natware", "natware.exe", "v",        true  },
		{ "editor",  "editor.exe",  "editor-v", false },
		{ "nexec",   "nexec.exe",   "nexec-v",  false },
	};

	struct comp_t : updater::component_t
	{
		std::wstring path;          // the exe on disk
		std::string repo, prefix, asset;
		bool self = false;
		bool enabled = true;
		bool known = false;         // `installed` is known (natware's own, the manifest or a version resource)
		int choice = -1;            // ticked (1) or unticked (0) in the launcher; -1 = the default
		// The exe as last read, so it's only hashed again when it changes.
		std::uint64_t disk_size = 0, disk_time = 0;
		std::string disk_sha;
		// Its newest release.
		std::string asset_url, sha256;
		std::uint64_t size = 0;
		bool downloaded = false;    // "<path>.download" is there and matches sha256
		bool went_in = false;       // put in place during this run
		std::uint64_t written = 0;
	};

	std::mutex g_mtx;
	std::vector<comp_t> g_comps;
	updater::state_t g_phase = updater::state_t::off;   // off, checking, current (checked), installing
	std::string g_error;                                // the last check or install that failed

	std::atomic<bool> g_busy{ false };     // a check or an install is running
	std::atomic<bool> g_watching{ false };
	bool g_restart_after = true;           // install: restart into a new natware now, or wait for natware to close
	std::wstring g_restart_args;
	bool g_restart_args_set = false;
	void (*g_before_restart)() = nullptr;

	std::wstring widen(const std::string& s)
	{
		if (s.empty())
			return {};
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
		return w;
	}

	std::string lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	std::wstring exe_path()
	{
		std::wstring path(32768, L'\0');
		const DWORD n = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
		path.resize(n);
		return path;
	}

	// The folder natware.exe is in, with the trailing backslash.
	std::wstring app_dir()
	{
		const std::wstring exe = exe_path();
		return exe.substr(0, exe.find_last_of(L'\\') + 1);
	}

	// "C:\...\editor.exe" -> "C:\...\editor.old.exe"
	std::wstring old_path(const std::wstring& exe)
	{
		const size_t dot = exe.find_last_of(L'.');
		return (dot == std::wstring::npos ? exe : exe.substr(0, dot)) + L".old.exe";
	}

	std::wstring download_path(const std::wstring& exe) { return exe + L".download"; }

	bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

	// What natware was started with, after its own path.
	std::wstring own_args()
	{
		const wchar_t* p = GetCommandLineW();
		if (*p == L'"')
		{
			++p;
			while (*p && *p != L'"')
				++p;
			if (*p)
				++p;
		}
		else
			while (*p && *p != L' ' && *p != L'\t')
				++p;
		while (*p == L' ' || *p == L'\t')
			++p;
		return p;
	}

	// ---- HTTP ---------------------------------------------------------------------------------

	struct response_t
	{
		DWORD status = 0;
		std::string body;
	};

	// One GET. Redirects are followed by hand, so the token is never sent on to another host
	// (GitHub hands asset downloads to a storage host that refuses a second credential).
	// `sink` gets the body in pieces instead of `out.body` when it's given.
	bool http_get(std::string url, std::vector<std::string> headers, response_t& out, std::string& error,
		const std::function<bool(const char*, size_t, std::uint64_t)>& sink = {})
	{
		HINTERNET session = WinHttpOpen(L"natware-updater/" NATWARE_VERSION_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session)
			session = WinHttpOpen(L"natware-updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session)
		{
			error = "couldn't start WinHTTP";
			return false;
		}
		WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);

		std::wstring first_host;
		bool ok = false;
		for (int hop = 0; hop < 6; ++hop)
		{
			const std::wstring wurl = widen(url);
			URL_COMPONENTS parts{};
			parts.dwStructSize = sizeof(parts);
			wchar_t host[256]{}, path[4096]{}, extra[4096]{};
			parts.lpszHostName = host;  parts.dwHostNameLength = 256;
			parts.lpszUrlPath = path;   parts.dwUrlPathLength = 4096;
			parts.lpszExtraInfo = extra; parts.dwExtraInfoLength = 4096;
			if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts))
			{
				error = "bad address: " + url;
				break;
			}
			if (hop == 0)
				first_host = host;
			else if (first_host != host)
			{
				// Another host: the token stays behind.
				headers.erase(std::remove_if(headers.begin(), headers.end(), [](const std::string& h)
					{ return _strnicmp(h.c_str(), "Authorization:", 14) == 0; }), headers.end());
			}

			HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0);
			const std::wstring object = std::wstring(path) + extra;
			HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
			if (!request)
			{
				error = "couldn't connect";
				if (connect) WinHttpCloseHandle(connect);
				break;
			}
			DWORD no_redirects = WINHTTP_DISABLE_REDIRECTS;
			WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &no_redirects, sizeof(no_redirects));

			std::wstring header_block;
			for (const std::string& h : headers)
				header_block += widen(h) + L"\r\n";

			bool sent = WinHttpSendRequest(request, header_block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_block.c_str(),
				header_block.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
				&& WinHttpReceiveResponse(request, nullptr);
			DWORD status = 0, len = sizeof(status);
			if (sent)
				WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);

			bool redirected = false;
			if (sent && status >= 300 && status < 400)
			{
				wchar_t location[4096]{};
				DWORD loc_len = sizeof(location);
				if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location, &loc_len, WINHTTP_NO_HEADER_INDEX))
				{
					const int n = WideCharToMultiByte(CP_UTF8, 0, location, -1, nullptr, 0, nullptr, nullptr);
					std::string next(n > 0 ? n - 1 : 0, '\0');
					WideCharToMultiByte(CP_UTF8, 0, location, -1, next.data(), n, nullptr, nullptr);
					url = next;
					redirected = true;
				}
			}

			if (sent && !redirected)
			{
				out.status = status;
				std::uint64_t total = 0;
				wchar_t length_text[32]{};
				DWORD length_len = sizeof(length_text);
				if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length_text, &length_len, WINHTTP_NO_HEADER_INDEX))
					total = _wcstoui64(length_text, nullptr, 10);

				ok = true;
				std::vector<char> buffer(64 * 1024);
				for (;;)
				{
					DWORD available = 0;
					if (!WinHttpQueryDataAvailable(request, &available))
					{
						ok = false;
						error = "the download was cut off";
						break;
					}
					if (available == 0)
						break;
					DWORD read = 0;
					const DWORD chunk = available < buffer.size() ? available : static_cast<DWORD>(buffer.size());
					if (!WinHttpReadData(request, buffer.data(), chunk, &read) || read == 0)
					{
						ok = false;
						error = "the download was cut off";
						break;
					}
					if (sink && status == 200)
					{
						if (!sink(buffer.data(), read, total))
						{
							ok = false;
							error = "couldn't write the download";
							break;
						}
					}
					else
						out.body.append(buffer.data(), read);
				}
			}
			else if (!sent)
				error = "couldn't reach " + std::string(url.begin(), url.begin() + (std::min<size_t>)(url.size(), 60));

			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connect);
			if (!redirected)
				break;
		}
		WinHttpCloseHandle(session);
		return ok;
	}

	std::vector<std::string> api_headers(bool binary)
	{
		std::vector<std::string> h{
			binary ? "Accept: application/octet-stream" : "Accept: application/vnd.github+json",
			"X-GitHub-Api-Version: 2022-11-28",
		};
		if (!external_config::update_token.empty())
			h.push_back("Authorization: Bearer " + external_config::update_token);
		return h;
	}

	// ---- SHA-256 ------------------------------------------------------------------------------

	struct sha256_t
	{
		BCRYPT_ALG_HANDLE alg = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		bool ok = false;

		sha256_t()
		{
			ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))
				&& BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0));
		}
		~sha256_t()
		{
			if (hash) BCryptDestroyHash(hash);
			if (alg) BCryptCloseAlgorithmProvider(alg, 0);
		}
		void add(const void* data, size_t n)
		{
			ok = ok && BCRYPT_SUCCESS(BCryptHashData(hash, (PUCHAR)data, (ULONG)n, 0));
		}
		std::string hex()
		{
			unsigned char digest[32]{};
			if (!ok || !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0)))
				return {};
			char text[65]{};
			for (int i = 0; i < 32; ++i)
				sprintf_s(text + i * 2, 3, "%02x", digest[i]);
			return text;
		}
	};

	std::string file_sha256(const std::wstring& path)
	{
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return {};
		sha256_t hash;
		std::vector<char> buffer(256 * 1024);
		DWORD read = 0;
		while (ReadFile(file, buffer.data(), (DWORD)buffer.size(), &read, nullptr) && read > 0)
			hash.add(buffer.data(), read);
		CloseHandle(file);
		return hash.hex();
	}

	// The exe's own version resource ("1.2.0"), "" when it has none.
	std::string file_version(const std::wstring& path)
	{
		DWORD ignored = 0;
		const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
		if (!size)
			return {};
		std::vector<char> data(size);
		VS_FIXEDFILEINFO* info = nullptr;
		UINT info_len = 0;
		if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())
			|| !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &info_len) || !info || info_len < sizeof(*info))
			return {};
		const unsigned a = HIWORD(info->dwProductVersionMS), b = LOWORD(info->dwProductVersionMS);
		const unsigned c = HIWORD(info->dwProductVersionLS), d = LOWORD(info->dwProductVersionLS);
		if (!a && !b && !c && !d)
			return {};
		return std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c) + (d ? "." + std::to_string(d) : "");
	}

	// ---- data\versions.json: what's installed -------------------------------------------------
	// { "natware": { "version": "1.0.2", "sha256": "..." }, "editor": {...}, "nexec": {...} }

	std::wstring manifest_path() { return app_dir() + L"data\\versions.json"; }

	nlohmann::json read_manifest()
	{
		HANDLE file = CreateFileW(manifest_path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return nlohmann::json::object();
		std::string text;
		char buffer[4096];
		DWORD read = 0;
		while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0)
			text.append(buffer, read);
		CloseHandle(file);
		nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
		return j.is_object() ? j : nlohmann::json::object();
	}

	// Written beside and moved over, so a crash never leaves half a file.
	void write_manifest(const nlohmann::json& j)
	{
		CreateDirectoryW((app_dir() + L"data").c_str(), nullptr);
		const std::wstring target = manifest_path(), temp = target + L".tmp";
		HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return;
		const std::string text = j.dump(2) + "\n";
		DWORD written = 0;
		const bool ok = WriteFile(file, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size();
		CloseHandle(file);
		if (!ok || !MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
			DeleteFileW(temp.c_str());
	}

	std::mutex g_manifest_mtx;

	void record(const std::string& name, const std::string& version, const std::string& sha)
	{
		std::lock_guard<std::mutex> lock(g_manifest_mtx);
		nlohmann::json j = read_manifest();
		const nlohmann::json entry = { { "version", version }, { "sha256", sha } };
		if (j.contains(name) && j[name] == entry)
			return;
		j[name] = entry;
		write_manifest(j);
	}

	// ---- the component list -------------------------------------------------------------------

	// Builds the list once, after data\settings.json has been read. Called with g_mtx held.
	void ensure_list()
	{
		if (!g_comps.empty())
			return;
		const std::wstring dir = app_dir();
		for (const spec_t& s : kSpecs)
		{
			comp_t c;
			c.name = s.name;
			c.file = s.file;
			c.self = s.self;
			c.path = s.self ? exe_path() : dir + widen(s.file);
			c.prefix = s.prefix;
			c.asset = s.file;
			c.repo = external_config::update_repo;
			const auto o = external_config::update_components.find(s.name);
			if (o != external_config::update_components.end())
			{
				if (!o->second.repo.empty()) c.repo = o->second.repo;
				if (!o->second.tag_prefix.empty()) c.prefix = o->second.tag_prefix;
				if (!o->second.asset.empty()) c.asset = o->second.asset;
				c.enabled = o->second.enabled;
			}
			if (c.repo.empty() || !c.enabled)
				c.enabled = false;
			g_comps.push_back(c);
		}
	}

	comp_t* find(const std::string& name)
	{
		for (comp_t& c : g_comps)
			if (c.name == name)
				return &c;
		return nullptr;
	}

	void default_selection(comp_t& c)
	{
		// A local build (present, version unknown) is only replaced when you tick it.
		c.selected = c.choice >= 0 ? (c.available && c.choice == 1) : (c.available && (c.known || !c.present));
	}

	// Reads what's on disk: whether each exe is there, its version, and its hash. The manifest
	// follows: an entry whose exe was rebuilt or removed is dropped.
	void scan_local()
	{
		struct disk_t { std::wstring path; std::uint64_t size = 0, time = 0; bool present = false; std::string sha, prev_sha; std::uint64_t prev_size = 0, prev_time = 0; };
		std::vector<disk_t> disk;
		{
			std::lock_guard<std::mutex> lock(g_mtx);
			ensure_list();
			for (const comp_t& c : g_comps)
				disk.push_back({ c.path, 0, 0, false, {}, c.disk_sha, c.disk_size, c.disk_time });
		}
		for (disk_t& d : disk)
		{
			WIN32_FILE_ATTRIBUTE_DATA a{};
			d.present = GetFileAttributesExW(d.path.c_str(), GetFileExInfoStandard, &a) != 0;
			if (!d.present)
				continue;
			d.size = (std::uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
			d.time = (std::uint64_t(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
			d.sha = !d.prev_sha.empty() && d.size == d.prev_size && d.time == d.prev_time ? d.prev_sha : file_sha256(d.path);
		}

		std::lock_guard<std::mutex> manifest_lock(g_manifest_mtx);
		nlohmann::json manifest = read_manifest();
		bool changed = false;
		std::lock_guard<std::mutex> lock(g_mtx);
		for (size_t i = 0; i < g_comps.size(); ++i)
		{
			comp_t& c = g_comps[i];
			const disk_t& d = disk[i];
			if (c.self && c.went_in)
				continue;   // the exe on disk is the new natware; this one is still the old
			c.present = d.present;
			c.disk_size = d.size;
			c.disk_time = d.time;
			c.disk_sha = d.sha;
			c.known = false;
			c.installed.clear();
			if (d.present)
			{
				const nlohmann::json* entry = manifest.contains(c.name) && manifest[c.name].is_object() ? &manifest[c.name] : nullptr;
				if (c.self)
					c.installed = NATWARE_VERSION;
				else if (entry && entry->value("sha256", "") == d.sha && !entry->value("version", "").empty())
					c.installed = entry->value("version", "");
				else if (!c.sha256.empty() && c.sha256 == d.sha)
					c.installed = c.latest;   // the same file as its newest release
				else
					c.installed = file_version(d.path);
				c.known = !c.installed.empty();
			}
			const nlohmann::json want = { { "version", c.installed }, { "sha256", d.sha } };
			if (c.known && (!manifest.contains(c.name) || manifest[c.name] != want))
			{
				manifest[c.name] = want;
				changed = true;
			}
			else if (!c.known && manifest.contains(c.name))
			{
				manifest.erase(c.name);
				changed = true;
			}
		}
		if (changed)
			write_manifest(manifest);
	}

	// The SHA-256 of `asset` in a release: GitHub's digest, else a line of the notes naming the
	// asset, else the notes' only "sha256:" line when the release has just this one exe.
	std::string release_sha(const nlohmann::json& release, const nlohmann::json& asset, const std::string& name, int exe_assets)
	{
		const std::string digest = asset.contains("digest") && asset["digest"].is_string() ? asset["digest"].get<std::string>() : "";
		if (digest.rfind("sha256:", 0) == 0 && digest.size() == 7 + 64)
			return lower(digest.substr(7));
		if (!release.contains("body") || !release["body"].is_string())
			return {};
		const std::string body = release["body"].get<std::string>();
		static const std::regex hex("[0-9a-fA-F]{64}");
		size_t start = 0;
		while (start < body.size())
		{
			size_t end = body.find('\n', start);
			if (end == std::string::npos)
				end = body.size();
			const std::string line = body.substr(start, end - start);
			std::smatch m;
			if (lower(line).find(lower(name)) != std::string::npos && std::regex_search(line, m, hex))
				return lower(m[0].str());
			start = end + 1;
		}
		if (exe_assets <= 1)
		{
			static const std::regex pattern("sha256:\\s*([0-9a-fA-F]{64})");
			std::smatch m;
			if (std::regex_search(body, m, pattern))
				return lower(m[1].str());
		}
		return {};
	}

	// The releases of one repository, newest first as GitHub lists them.
	bool fetch_releases(const std::string& repo, nlohmann::json& out, std::string& error)
	{
		response_t r;
		const std::string url = external_config::update_api + "/repos/" + repo + "/releases?per_page=100";
		if (!http_get(url, api_headers(false), r, error))
		{
			error = "couldn't reach GitHub";
			return false;
		}
		if (r.status == 404)
		{
			error = external_config::update_token.empty() ? "no releases yet (or the repository is private)" : "no releases yet";
			return false;
		}
		if (r.status == 401 || r.status == 403)
		{
			error = "GitHub refused the update token";
			return false;
		}
		if (r.status != 200)
		{
			error = "GitHub answered " + std::to_string(r.status);
			return false;
		}
		out = nlohmann::json::parse(r.body, nullptr, false);
		if (out.is_discarded() || !out.is_array())
		{
			error = "GitHub's answer didn't make sense";
			return false;
		}
		return true;
	}

	struct found_t
	{
		std::string version, asset_url, sha256, problem;
		std::uint64_t size = 0;
	};

	// The newest published release tagged "<prefix><version>" and what it carries for `asset`.
	found_t newest_release(const nlohmann::json& releases, const std::string& prefix, const std::string& asset)
	{
		found_t best;
		const nlohmann::json* chosen = nullptr;
		for (const auto& rel : releases)
		{
			if (!rel.is_object() || rel.value("draft", false) || rel.value("prerelease", false))
				continue;
			const std::string tag = rel.contains("tag_name") && rel["tag_name"].is_string() ? rel["tag_name"].get<std::string>() : "";
			if (tag.size() <= prefix.size() || _strnicmp(tag.c_str(), prefix.c_str(), prefix.size()) != 0
				|| !std::isdigit((unsigned char)tag[prefix.size()]))
				continue;
			const std::string version = tag.substr(prefix.size());
			if (!chosen || updater::compare_versions(version, best.version) > 0)
			{
				chosen = &rel;
				best.version = version;
			}
		}
		if (!chosen)
			return best;

		int exe_assets = 0;
		const nlohmann::json* match = nullptr;
		if (chosen->contains("assets") && (*chosen)["assets"].is_array())
			for (const auto& a : (*chosen)["assets"])
			{
				if (!a.is_object())
					continue;
				const std::string n = lower(a.value("name", ""));
				if (n.size() > 4 && n.compare(n.size() - 4, 4, ".exe") == 0)
					++exe_assets;
				if (n == lower(asset))
					match = &a;
			}
		if (!match)
		{
			best.problem = "the release has no " + asset;
			return best;
		}
		best.asset_url = match->value("url", "");
		best.size = match->value("size", 0ull);
		best.sha256 = release_sha(*chosen, *match, asset, exe_assets);
		if (best.asset_url.empty())
			best.problem = "the release has no " + asset;
		else if (best.sha256.size() != 64)
			best.problem = "the release has no SHA-256 for " + asset;
		return best;
	}

	// ---- installing ---------------------------------------------------------------------------

	// Downloads one component's newest release into "<exe>.download" and checks it.
	bool stage_one(size_t index, std::string& error)
	{
		comp_t c;
		{
			std::lock_guard<std::mutex> lock(g_mtx);
			c = g_comps[index];
			g_comps[index].state = updater::state_t::downloading;
			g_comps[index].progress = 0.0f;
			g_comps[index].written = 0;
			g_comps[index].downloaded = false;
		}
		if (c.asset_url.empty() || c.sha256.size() != 64)
		{
			error = "nothing to download";
			return false;
		}

		const std::wstring target = download_path(c.path);
		HANDLE file = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			error = "couldn't write next to " + c.file;
			return false;
		}

		sha256_t hash;
		std::uint64_t written = 0;
		char head[2]{};
		response_t r;
		const bool got = http_get(c.asset_url, api_headers(true), r, error, [&](const char* data, size_t n, std::uint64_t total)
		{
			if (written < 2)
				for (size_t i = 0; i < n && written + i < 2; ++i)
					head[written + i] = data[i];
			DWORD out = 0;
			if (!WriteFile(file, data, (DWORD)n, &out, nullptr) || out != n)
				return false;
			hash.add(data, n);
			written += n;
			const std::uint64_t expect = c.size ? c.size : total;
			std::lock_guard<std::mutex> lock(g_mtx);
			g_comps[index].written = written;
			if (expect)
				g_comps[index].progress = (std::min)(1.0f, (float)written / (float)expect);
			return true;
		});
		CloseHandle(file);

		auto fail = [&](const std::string& why)
		{
			error = why;
			DeleteFileW(target.c_str());
			return false;
		};
		if (!got)
			return fail(error.empty() ? "the download failed" : error);
		if (r.status == 401 || r.status == 403 || r.status == 404)
			return fail(external_config::update_token.empty()
				? "GitHub wouldn't hand over the file: a private repository needs update_token in data\\settings.json"
				: "GitHub refused the update token for the download");
		if (r.status != 200)
			return fail("the download answered " + std::to_string(r.status));
		if (c.size && written != c.size)
			return fail("the download came out the wrong size");
		if (head[0] != 'M' || head[1] != 'Z')
			return fail("the download isn't a program");
		if (hash.hex() != c.sha256)
			return fail("the download didn't match its SHA-256: nothing was changed");

		std::lock_guard<std::mutex> lock(g_mtx);
		g_comps[index].downloaded = true;
		g_comps[index].progress = 1.0f;
		return true;
	}

	// Puts one downloaded component in place. A running exe can be renamed but not overwritten,
	// so the one there steps aside for the new one (and is deleted on natware's next start).
	bool swap_one(size_t index, std::string& error)
	{
		comp_t c;
		{
			std::lock_guard<std::mutex> lock(g_mtx);
			c = g_comps[index];
		}
		const std::wstring fresh = download_path(c.path);
		const std::wstring old = old_path(c.path);
		if (!c.downloaded || !exists(fresh))
		{
			error = "the download of " + c.file + " is missing";
			return false;
		}
		const bool had = exists(c.path);
		DeleteFileW(old.c_str());
		if (had && !MoveFileExW(c.path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			error = "couldn't move " + c.file + " aside (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		if (!MoveFileExW(fresh.c_str(), c.path.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			const DWORD code = GetLastError();
			if (had)
				MoveFileExW(old.c_str(), c.path.c_str(), MOVEFILE_REPLACE_EXISTING);   // put it back as it was
			error = "couldn't put the new " + c.file + " in place (error " + std::to_string(code) + ")";
			return false;
		}
		record(c.name, c.latest, c.sha256);
		std::lock_guard<std::mutex> lock(g_mtx);
		comp_t& live = g_comps[index];
		live.downloaded = false;
		live.went_in = true;
		live.present = true;
		live.known = true;
		live.installed = live.latest;
		live.disk_sha = live.sha256;
		live.disk_size = 0;   // read again on the next scan
		live.available = false;
		live.selected = false;
		live.choice = -1;
		live.state = updater::state_t::done;
		return true;
	}

	// The ticked updates, marked as downloading so the launcher shows it at once.
	std::vector<size_t> take_batch()
	{
		std::vector<size_t> batch;
		std::lock_guard<std::mutex> lock(g_mtx);
		for (size_t i = 0; i < g_comps.size(); ++i)
			if (g_comps[i].available && g_comps[i].selected && g_comps[i].state != updater::state_t::staged)
			{
				batch.push_back(i);
				g_comps[i].state = updater::state_t::downloading;
				g_comps[i].progress = 0.0f;
				g_comps[i].written = 0;
			}
		if (!batch.empty())
		{
			g_phase = updater::state_t::installing;
			g_error.clear();
		}
		return batch;
	}

	// Every exe of the batch is downloaded and checked before anything changes; one that fails
	// leaves them all as they were.
	bool stage_batch(const std::vector<size_t>& batch, std::string& error)
	{
		for (size_t i : batch)
		{
			std::string why;
			if (stage_one(i, why))
				continue;
			std::lock_guard<std::mutex> lock(g_mtx);
			for (size_t j : batch)
			{
				DeleteFileW(download_path(g_comps[j].path).c_str());
				g_comps[j].downloaded = false;
				g_comps[j].state = updater::state_t::available;
				g_comps[j].progress = 0.0f;
			}
			error = g_comps[i].name + ": " + why;
			g_comps[i].state = updater::state_t::failed;
			g_comps[i].message = why;
			g_error = error;
			g_phase = updater::state_t::current;
			return false;
		}
		return true;
	}

	void finish_batch(const std::string& error)
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		g_phase = updater::state_t::current;
		if (!error.empty())
			g_error = error;
	}

	// Starts the exe at `path` (the new natware) on its own.
	bool launch(const std::wstring& path, const std::wstring& args)
	{
		std::wstring command = L"\"" + path + L"\"" + (args.empty() ? L"" : L" " + args);
		const size_t slash = path.find_last_of(L'\\');
		const std::wstring dir = slash == std::wstring::npos ? L"." : path.substr(0, slash);
		STARTUPINFOW si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};
		if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))
			return false;
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}

	void install_worker(std::vector<size_t> batch)
	{
		std::string error;
		if (!stage_batch(batch, error))
		{
			g_busy = false;
			return;
		}

		// The editor and nexec go in now (even while they run: they get the new one next start).
		std::string failed;
		size_t self = SIZE_MAX;
		for (size_t i : batch)
		{
			bool is_self;
			{
				std::lock_guard<std::mutex> lock(g_mtx);
				is_self = g_comps[i].self;
			}
			if (is_self)
			{
				self = i;
				continue;
			}
			if (!swap_one(i, error))
			{
				std::lock_guard<std::mutex> lock(g_mtx);
				g_comps[i].state = updater::state_t::failed;
				g_comps[i].message = error;
				failed = g_comps[i].name + ": " + error;
			}
		}

		if (self != SIZE_MAX)
		{
			if (!g_restart_after)
			{
				// Downloaded and checked: natware goes in when it closes (finish_on_quit).
				std::lock_guard<std::mutex> lock(g_mtx);
				g_comps[self].state = updater::state_t::staged;
			}
			else if (!swap_one(self, error))
			{
				std::lock_guard<std::mutex> lock(g_mtx);
				g_comps[self].state = updater::state_t::failed;
				g_comps[self].message = error;
				failed = "natware: " + error;
			}
			else
			{
				if (g_before_restart)
					g_before_restart();
				const std::wstring path = exe_path();
				if (!launch(path, g_restart_args_set ? g_restart_args : own_args()))
					failed = "installed, but couldn't start the new version: open natware again";
				else
				{
					finish_batch(failed);
					settings::globals::quit_requested = true;   // the new natware is up: this one closes
					g_busy = false;
					return;
				}
			}
		}
		finish_batch(failed);
		g_busy = false;
	}

	std::string join(const std::vector<std::string>& parts)
	{
		std::string out;
		for (const std::string& p : parts)
			out += (out.empty() ? "" : ", ") + p;
		return out;
	}
}

const char* updater::version()
{
	return NATWARE_VERSION;
}

updater::status_t updater::status()
{
	std::lock_guard<std::mutex> lock(g_mtx);
	ensure_list();
	status_t st;
	bool any_enabled = false;
	for (const comp_t& c : g_comps)
		any_enabled = any_enabled || c.enabled;
	if (!any_enabled)
		return st;   // off

	std::vector<std::string> ticked, staged, done;
	const comp_t* only = nullptr;
	std::uint64_t total = 0, written = 0;
	bool downloading = false;
	for (const comp_t& c : g_comps)
	{
		const std::string label = c.name + " " + c.latest;
		if (c.state == state_t::downloading)
			downloading = true;
		if (c.available && c.selected && c.state != state_t::staged)
		{
			ticked.push_back(label);
			only = &c;
			st.restarts = st.restarts || c.self;
			total += c.size;
			written += c.written;
		}
		else if (c.available && c.state != state_t::staged)
			++st.optional;
		if (c.state == state_t::staged)
			staged.push_back(label);
		if (c.state == state_t::done)
			done.push_back(label);
	}

	auto name = [&](const std::vector<std::string>& v)
	{
		st.latest = join(v);
		st.count = (int)v.size();
	};
	if (g_phase == state_t::checking)
		st.state = state_t::checking;
	else if (g_phase == state_t::installing)
	{
		st.state = downloading ? state_t::downloading : state_t::installing;
		name(ticked);
		st.progress = total ? (std::min)(1.0f, (float)written / (float)total) : 0.0f;
	}
	else if (g_phase == state_t::off)
		st.state = state_t::off;
	else if (!g_error.empty())
	{
		st.state = state_t::failed;
		st.message = g_error;
		name(ticked);
	}
	else if (!ticked.empty())
	{
		st.state = state_t::available;
		name(ticked);
	}
	else if (!staged.empty())
	{
		st.state = state_t::staged;
		name(staged);
	}
	else if (!done.empty())
	{
		st.state = state_t::done;
		name(done);
	}
	else
		st.state = state_t::current;
	if (st.count == 1 && only)
		st.have = only->installed;
	return st;
}

std::vector<updater::component_t> updater::components()
{
	std::lock_guard<std::mutex> lock(g_mtx);
	ensure_list();
	std::vector<component_t> out;
	for (const comp_t& c : g_comps)
	{
		component_t v = c;
		if (!c.enabled)
			v.state = state_t::off;
		out.push_back(v);
	}
	return out;
}

void updater::select(const std::string& name, bool on)
{
	std::lock_guard<std::mutex> lock(g_mtx);
	if (comp_t* c = find(name))
	{
		c->choice = on ? 1 : 0;
		default_selection(*c);
	}
}

std::wstring updater::path_of(const std::string& name)
{
	std::lock_guard<std::mutex> lock(g_mtx);
	ensure_list();
	const comp_t* c = find(name);
	return c ? c->path : std::wstring();
}

bool updater::updated(const std::string& name)
{
	std::lock_guard<std::mutex> lock(g_mtx);
	const comp_t* c = find(name);
	return c && c->went_in;
}

void updater::set_restart(const std::wstring& args, void (*before)())
{
	g_restart_args = args;
	g_restart_args_set = true;
	g_before_restart = before;
}

int updater::compare_versions(const std::string& a, const std::string& b)
{
	auto parts = [](const std::string& v)
	{
		std::vector<long> out;
		long cur = 0;
		bool any = false;
		const size_t start = !v.empty() && (v[0] == 'v' || v[0] == 'V') ? 1 : 0;
		for (size_t i = start; i < v.size(); ++i)
		{
			const char c = v[i];
			if (c >= '0' && c <= '9')
			{
				cur = cur * 10 + (c - '0');
				any = true;
			}
			else if (c == '.')
			{
				out.push_back(cur);
				cur = 0;
				any = false;
			}
			else
				break;   // "-beta" and the like end the number
		}
		if (any || out.empty())
			out.push_back(cur);
		return out;
	};
	const std::vector<long> x = parts(a), y = parts(b);
	for (size_t i = 0; i < (std::max)(x.size(), y.size()); ++i)
	{
		const long p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
		if (p != q)
			return p < q ? -1 : 1;
	}
	return 0;
}

void updater::cleanup()
{
	std::vector<std::wstring> paths;
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		ensure_list();
		for (const comp_t& c : g_comps)
			paths.push_back(c.path);
	}
	// A download cut short by an earlier run never goes in.
	for (const std::wstring& p : paths)
		DeleteFileW(download_path(p).c_str());
	scan_local();   // the launcher lists what's installed before the first check answers

	// An old exe can still be closing (natware right after an update) or still running (a
	// nexec that was replaced while it ran): keep trying for a few seconds, and again next start.
	std::thread([paths]
	{
		std::vector<std::wstring> left;
		for (const std::wstring& p : paths)
			left.push_back(old_path(p));
		for (int i = 0; i < 40 && !left.empty(); ++i)
		{
			left.erase(std::remove_if(left.begin(), left.end(), [](const std::wstring& old)
				{ return !exists(old) || DeleteFileW(old.c_str()); }), left.end());
			if (!left.empty())
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
	}).detach();
}

bool updater::check_now(std::string& error)
{
	scan_local();

	std::map<std::string, std::vector<size_t>> by_repo;
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		for (size_t i = 0; i < g_comps.size(); ++i)
			if (g_comps[i].enabled)
				by_repo[g_comps[i].repo].push_back(i);
		if (by_repo.empty())
		{
			g_phase = state_t::off;
			return true;
		}
		g_phase = state_t::checking;
	}

	int reached = 0;
	std::string first_error;
	for (const auto& [repo, members] : by_repo)
	{
		nlohmann::json releases;
		std::string why;
		const bool ok = fetch_releases(repo, releases, why);
		if (ok)
			++reached;
		else if (first_error.empty())
			first_error = why;

		std::lock_guard<std::mutex> lock(g_mtx);
		for (size_t i : members)
		{
			comp_t& c = g_comps[i];
			if (c.state == state_t::staged)
				continue;   // a natware waiting to go in on exit stays as it is
			c.message.clear();
			if (!ok)
			{
				c.state = state_t::failed;
				c.message = why;
				continue;
			}
			const found_t f = newest_release(releases, c.prefix, c.asset);
			c.latest = f.version;
			c.asset_url = f.asset_url;
			c.sha256 = f.sha256;
			c.size = f.size;
			if (c.present && !c.known && !c.sha256.empty() && c.sha256 == c.disk_sha)
			{
				c.installed = c.latest;   // the same file as the release
				c.known = true;
			}
			const bool newer = !c.latest.empty() && (!c.present || !c.known || compare_versions(c.latest, c.installed) > 0);
			c.available = newer && f.problem.empty();
			if (c.latest.empty())
			{
				c.state = state_t::current;
				c.message = "no releases";
			}
			else if (newer && !f.problem.empty())
			{
				c.state = state_t::failed;
				c.message = f.problem;
			}
			else
				c.state = c.available ? state_t::available : state_t::current;
			default_selection(c);
		}
	}

	// A component found the same as its release gets that version in the manifest.
	{
		std::vector<std::pair<std::string, std::pair<std::string, std::string>>> learned;
		{
			std::lock_guard<std::mutex> lock(g_mtx);
			for (const comp_t& c : g_comps)
				if (!c.self && c.present && c.known && !c.sha256.empty() && c.sha256 == c.disk_sha)
					learned.push_back({ c.name, { c.installed, c.disk_sha } });
		}
		for (const auto& l : learned)
			record(l.first, l.second.first, l.second.second);
	}

	std::lock_guard<std::mutex> lock(g_mtx);
	g_phase = state_t::current;
	if (reached == 0)
	{
		error = first_error;
		g_error = first_error;
		return false;
	}
	g_error.clear();
	// natware's own release missing its exe or hash is still worth saying, as before.
	for (const comp_t& c : g_comps)
		if (c.enabled && c.state == state_t::failed && c.self)
		{
			error = c.message;
			g_error = c.message;
			return false;
		}
	return true;
}

bool updater::stage_now(std::string& error)
{
	const std::vector<size_t> batch = take_batch();
	if (batch.empty())
	{
		error = "nothing newer to download";
		return false;
	}
	const bool ok = stage_batch(batch, error);
	std::lock_guard<std::mutex> lock(g_mtx);
	if (ok)
		for (size_t i : batch)
			g_comps[i].state = state_t::staged;
	g_phase = state_t::current;
	return ok;
}

bool updater::swap_now(std::string& error)
{
	std::vector<size_t> ready;
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		for (size_t i = 0; i < g_comps.size(); ++i)
			if (g_comps[i].downloaded)
				ready.push_back(i);
	}
	if (ready.empty())
	{
		error = "the download is missing";
		return false;
	}
	for (size_t i : ready)
		if (!swap_one(i, error))
			return false;
	return true;
}

bool updater::install_now(const std::string& name, std::string& error)
{
	// Waits out a check that's running; an install that's running would be this one's twin.
	for (int i = 0; g_busy.exchange(true); ++i)
	{
		if (i > 600)
		{
			error = "another update is still running";
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	size_t index = SIZE_MAX;
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		ensure_list();
		for (size_t i = 0; i < g_comps.size(); ++i)
			if (g_comps[i].name == name && !g_comps[i].self)
				index = i;
		if (index == SIZE_MAX || !g_comps[index].available)
		{
			error = index == SIZE_MAX ? "unknown component" : "no release of " + name + " to download";
			g_busy = false;
			return false;
		}
		g_phase = state_t::installing;
		g_error.clear();
	}
	const bool ok = stage_batch({ index }, error) && swap_one(index, error);
	finish_batch(ok ? "" : error);
	g_busy = false;
	return ok;
}

void updater::check_async()
{
	// Once at the start, then every 10 minutes while natware is open, so a release published
	// while you play is offered too.
	bool expected = false;
	if (!g_watching.compare_exchange_strong(expected, true))
		return;
	{
		// "Checking" from the first frame, not "off" until the thread gets going.
		std::lock_guard<std::mutex> lock(g_mtx);
		ensure_list();
		for (const comp_t& c : g_comps)
			if (c.enabled && g_phase == state_t::off)
				g_phase = state_t::checking;
	}
	std::thread([]
	{
		for (;;)
		{
			const state_t s = status().state;
			const bool settled = s == state_t::available || s == state_t::downloading || s == state_t::installing || s == state_t::staged;
			if (!settled && !g_busy.exchange(true))
			{
				std::string error;
				check_now(error);
				g_busy = false;
			}
			std::this_thread::sleep_for(std::chrono::minutes(10));
		}
	}).detach();
}

void updater::install_async(bool restart)
{
	if (status().state != state_t::available || g_busy.exchange(true))
		return;
	g_restart_after = restart;
	std::vector<size_t> batch = take_batch();
	if (batch.empty())
	{
		g_busy = false;
		return;
	}
	std::thread(install_worker, std::move(batch)).detach();
}

bool updater::finish_on_quit()
{
	std::vector<size_t> staged;
	{
		std::lock_guard<std::mutex> lock(g_mtx);
		for (size_t i = 0; i < g_comps.size(); ++i)
			if (g_comps[i].state == state_t::staged && g_comps[i].downloaded)
				staged.push_back(i);
	}
	bool any = false;
	for (size_t i : staged)
	{
		std::string error;
		any = swap_one(i, error) || any;
	}
	return any;
}
