#pragma once
#include <string>

// natware used to keep its files beside natware.exe (data\, configs\, workspace\, editor.exe,
// nexec.exe). Now they live in the natware folder (paths.h), and natware.exe is the only file in
// the folder you run it from.
namespace layout
{
	// At startup: moves what an older natware left beside the exe into the natware folder. Folders
	// and files the natware folder already has are left where they are (never overwritten), except
	// editor.exe and nexec.exe, which go in when they're newer (a fresh build beside the exe).
	void adopt_from(const std::wstring& exe_dir);
}
