#pragma once

// natware's version. release.ps1 (beside natware.sln) rewrites these lines, builds, and
// publishes the exe as GitHub release "v<version>"; natware compares it with its newest release.
// The editor (editor\version.h) and nexec have versions of their own.
#define NATWARE_VERSION "1.1.3"
#define NATWARE_VERSION_RC 1,1,3,0
#define NATWARE_VERSION_W L"" NATWARE_VERSION
