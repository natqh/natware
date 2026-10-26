#pragma once
#include <string>
#include <vector>

// natware keeps three programs up to date from GitHub releases, each with its own version:
//   natware.exe  this launcher and the overlay
//   editor.exe   the script editor (natware starts it)
//   nexec.exe    the editor's backend (the editor starts it)
// natware.exe stays wherever you put it; the editor and nexec live in the natware folder's bin\
// (paths.h). Each is released on its own in the same repository: tags
// "v1.2.0" for natware (the tags older natware versions already look for), "editor-v1.2.0" and
// "nexec-v1.2.0" for the others. A release carries the exe as an asset and its SHA-256 (GitHub's
// asset digest, or a "sha256: <hex>" line in the notes; with several exes in one release, a
// line naming the exe). release.ps1 beside natware.sln publishes them that way.
//
// natware asks for the releases in the background at startup and every 10 minutes after. What
// a component has installed comes from data\versions.json (written whenever one goes in, with
// the exe's SHA-256 so a rebuilt exe isn't mistaken for the release), else from the exe's
// version resource. A component that's newer on GitHub, or missing, is offered; one whose
// version can't be known (a local build) is only replaced when you tick it in the launcher.
//
// Installing downloads every chosen exe and checks each against its SHA-256 before anything is
// changed. The editor and nexec go in right away: a running exe can be renamed, so the old one
// steps aside as "<name>.old.exe" and the new one takes its place for its next start. natware
// itself goes in the same way, then restarts into the new version, or waits until you close it.
//
// data\settings.json picks where updates come from:
//   "update_repo"  "owner/name" of the GitHub repository (empty turns updates off)
//   "update_token" a read-only token, only needed when the repository is private
//   "update_api"   the API address (https://api.github.com; the preview test points it at a
//                  local server)
//   "components"   optional, per component: { "editor": { "repo": "owner/other",
//                  "tag_prefix": "editor-v", "asset": "editor.exe", "enabled": true } }
namespace updater
{
	// `done` is new: something went in during this run and nothing waits for a restart.
	enum class state_t { off, checking, current, available, downloading, installing, staged, failed, done };

	struct component_t
	{
		std::string name;        // "natware", "editor", "nexec"
		std::string file;        // its exe beside natware.exe
		bool present = false;    // the exe is there
		std::string installed;   // its version; "" when it's missing or its version isn't known
		std::string latest;      // its newest release's version, "" when it has none
		bool available = false;  // that release is newer than what's installed, or it's missing
		bool selected = false;   // goes in with the next install
		state_t state = state_t::off;
		std::string message;     // why it failed, or "no releases"
		float progress = 0.0f;   // 0..1 while downloading
	};

	struct status_t
	{
		state_t state = state_t::off;
		std::string latest;     // what it's about, e.g. "natware 1.1.0" or "natware 1.1.0, editor 1.0.3"
		std::string have;       // with one component: its installed version ("" when missing or unknown)
		std::string message;    // what went wrong, when it failed
		float progress = 0.0f;  // 0..1 across the whole download
		int count = 0;          // how many components `latest` names
		int optional = 0;       // updates that are out but not ticked (local builds)
		bool restarts = false;  // natware itself is among them: installing now restarts it
	};

	const char* version();   // natware's own
	status_t status();
	std::vector<component_t> components();
	void select(const std::string& name, bool on);
	std::wstring path_of(const std::string& name);   // where a component's exe lives (bin\, or natware.exe itself)

	void cleanup();                    // at startup: reads what's installed, removes what an earlier update left behind
	void check_async();                // starts the background checks (once per run)
	void install_async(bool restart);  // the ticked updates; restart: natware restarts now if it's among them, else on_quit
	bool finish_on_quit();             // on exit: puts a staged natware in place
	bool updated(const std::string& name);   // went in during this run

	// How natware restarts into a new version: the arguments it starts with (by default the ones
	// it was started with) and what runs first (the editor closes, so it isn't opened twice).
	void set_restart(const std::wstring& args, void (*before)() = nullptr);

	// The same steps run synchronously, for the preview test (which doesn't start the new exe)
	// and for a component that's missing when it's needed.
	bool check_now(std::string& error);
	bool stage_now(std::string& error);   // download and verify the ticked ones into "<exe>.download"
	bool swap_now(std::string& error);    // move each exe to "<name>.old.exe", its download in its place
	bool install_now(const std::string& name, std::string& error);   // one component, downloaded and put in place

	// < 0, 0 or > 0 as version a is older, the same or newer than b ("1.2.10" > "1.2.9").
	int compare_versions(const std::string& a, const std::string& b);
}
