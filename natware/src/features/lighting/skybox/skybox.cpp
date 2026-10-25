#include "skybox.h"

#include <thread>
#include <chrono>

#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <sdk/sdk.h>
#include <game/game.h>
#include <settings.h>

// Skybox changing requires SkyValid offset to force render refresh.
// Current offset is 0x0 (invalid) so skybox cannot work externally.
// This is a stub that does nothing until a valid SkyValid offset is found.

namespace lighting
{
	namespace skybox
	{
		void run()
		{
			// Disabled — SkyValid offset is 0x0, cannot force sky texture reload externally
		}
	}
}
