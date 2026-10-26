#pragma once
#include <Windows.h>
#include <ShlObj.h>
#include <string>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// Where natware keeps everything but natware.exe itself, so the folder you run natware.exe from
// holds nothing else to mix it up with:
//   %LOCALAPPDATA%\natware\
//     bin\        editor.exe, nexec.exe (and nexec's main.luau)
//     data\       settings.json, last.json, offsets.json, versions.json, webview\ (editor storage)
//     configs\    your configs
//     workspace\  your scripts, listed in the editor's Explorer
//     autoexec\   scripts the editor runs after every attach
// NATWARE_ROOT overrides it: natware sets it for the programs it starts so they agree, and the
// preview tests point it at the folder beside the exe.
namespace natware_paths
{
	inline std::wstring with_slash(std::wstring p)
	{
		if (!p.empty() && p.back() != L'\\' && p.back() != L'/')
			p += L'\\';
		return p;
	}

	inline std::wstring root()
	{
		std::wstring dir;
		wchar_t over[32768]{};
		const DWORD n = GetEnvironmentVariableW(L"NATWARE_ROOT", over, 32768);
		if (n > 0 && n < 32768)
			dir = with_slash(over);
		else
		{
			PWSTR local = nullptr;
			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)) && local)
				dir = with_slash(local) + L"natware\\";
			CoTaskMemFree(local);
		}
		if (!dir.empty())
			SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
		return dir;
	}

	inline std::wstring sub(const wchar_t* name)
	{
		const std::wstring dir = root() + name + L"\\";
		CreateDirectoryW(dir.c_str(), nullptr);
		return dir;
	}

	inline std::wstring bin() { return sub(L"bin"); }
	inline std::wstring data() { return sub(L"data"); }
	inline std::wstring workspace() { return sub(L"workspace"); }
	inline std::wstring autoexec() { return sub(L"autoexec"); }
}
