#pragma once

// editor.exe's version, separate from natware's. release.ps1 (beside natware.sln) rewrites both
// lines, builds, and publishes the exe as GitHub release "editor-v<version>"; natware compares
// it with what's installed (data\versions.json, else this exe's version resource).
#define EDITOR_VERSION "1.0.1"
#define EDITOR_VERSION_RC 1,0,1,0
