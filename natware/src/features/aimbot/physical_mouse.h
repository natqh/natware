#pragma once
#include <cstdint>

// Counts mouse movement from your hand (not the moves natware sends), so the mouse aim modes can
// tell when you moved the mouse yourself and not learn your sensitivity from that.
namespace physical_mouse
{
	void start();                 // installs the watcher once (a low-level mouse hook on its own thread)
	std::uint32_t moves();        // grows with every real mouse movement
}
