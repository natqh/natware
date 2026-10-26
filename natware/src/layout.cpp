#include "layout.h"
#include "paths.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
	bool same_dir(const fs::path& a, const fs::path& b)
	{
		std::error_code ec;
		return fs::equivalent(a, b, ec);
	}

	// A rename when both are on one drive; a copy and delete otherwise.
	bool move(const fs::path& from, const fs::path& to)
	{
		std::error_code ec;
		fs::create_directories(to.parent_path(), ec);
		fs::rename(from, to, ec);
		if (!ec)
			return true;
		ec.clear();
		fs::copy(from, to, fs::copy_options::recursive, ec);
		if (ec)
			return false;
		fs::remove_all(from, ec);
		return true;
	}

	// Moves each file and folder in `from` that `to` doesn't have yet; `whole` names the folders
	// that only make sense in one piece (the editor's browser profile) and move as one or not at all.
	void merge(const fs::path& from, const fs::path& to, std::initializer_list<const wchar_t*> whole = {})
	{
		std::error_code ec;
		if (!fs::is_directory(from, ec))
			return;
		fs::create_directories(to, ec);
		for (const fs::directory_entry& entry : fs::directory_iterator(from, ec))
		{
			const fs::path target = to / entry.path().filename();
			bool unit = false;
			for (const wchar_t* w : whole)
				unit = unit || _wcsicmp(entry.path().filename().c_str(), w) == 0;
			if (entry.is_directory(ec) && !unit)
				merge(entry.path(), target);
			else if (!fs::exists(target, ec))
				move(entry.path(), target);
		}
		// Gone if everything in it moved; kept (with what didn't) otherwise.
		if (fs::is_empty(from, ec))
			fs::remove(from, ec);
	}

	std::uint64_t write_time(const fs::path& p)
	{
		std::error_code ec;
		return static_cast<std::uint64_t>(fs::last_write_time(p, ec).time_since_epoch().count());
	}

	// editor.exe and nexec.exe beside natware.exe: into bin\ when newer than the copy there (or
	// it has none), else removed, so the folder you run natware from only has natware.exe.
	void adopt_program(const fs::path& exe_dir, const fs::path& bin, const wchar_t* name)
	{
		std::error_code ec;
		const fs::path here = exe_dir / name, there = bin / name;
		if (!fs::exists(here, ec))
			return;
		if (!fs::exists(there, ec) || write_time(here) > write_time(there))
		{
			if (fs::exists(there, ec))
			{
				// A running copy can't be replaced but can be renamed; natware deletes it next start.
				fs::path aside = there;
				aside.replace_extension(L".old.exe");
				fs::remove(aside, ec);
				fs::rename(there, aside, ec);
			}
			move(here, there);
		}
		else
			fs::remove(here, ec);
		fs::path old = here;
		old.replace_extension(L".old.exe");
		fs::remove(old, ec);
	}
}

void layout::adopt_from(const std::wstring& exe_dir)
{
	const fs::path here(exe_dir), root(natware_paths::root());
	if (root.empty() || same_dir(here, root))
		return;

	merge(here / L"data", root / L"data", { L"webview" });
	merge(here / L"configs", root / L"configs");
	merge(here / L"workspace", root / L"workspace");
	merge(here / L"autoexec", root / L"autoexec");

	const fs::path bin(natware_paths::bin());
	adopt_program(here, bin, L"editor.exe");
	adopt_program(here, bin, L"nexec.exe");
	// nexec runs main.luau from beside itself.
	std::error_code ec;
	if (fs::exists(here / L"main.luau", ec) && !fs::exists(bin / L"main.luau", ec))
		move(here / L"main.luau", bin / L"main.luau");
}
