#include "exposure.h"

#include <thread>
#include <chrono>

#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <sdk/sdk.h>
#include <game/game.h>
#include <settings.h>

namespace lighting
{
	namespace exposure
	{
		struct OriginalExposure {
			float exposure;
			bool saved = false;
		};

		static OriginalExposure original;
		static bool was_enabled = false;

		static void exposure_thread() {
			using namespace std::chrono_literals;

			while (true) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));

				try {
					if (game::datamodel.address == 0) {
						continue;
					}

					rbx::instance_t lighting = game::datamodel.find_first_child("Lighting");
					if (lighting.address == 0) {
						lighting = game::datamodel.find_first_child_by_class("Lighting");
					}
					if (lighting.address == 0) {
						continue;
					}

					if (!settings::lighting::exposure::enabled) {
						if (was_enabled && original.saved) {
							memory->write<float>(lighting.address + Offsets::Lighting::ExposureCompensation, original.exposure);
							was_enabled = false;
						}
						continue;
					}

					if (!original.saved) {
						original.exposure = memory->read<float>(lighting.address + Offsets::Lighting::ExposureCompensation);
						original.saved = true;
					}

					memory->write<float>(lighting.address + Offsets::Lighting::ExposureCompensation, settings::lighting::exposure::exposure);
					was_enabled = true;
				}
				catch (...) {
				}
			}
		}

		void run() {
			static bool initialized = false;
			if (!initialized) {
				std::thread(exposure_thread).detach();
				initialized = true;
			}
		}
	}
}
