#pragma once
#include <d3d11.h>
#include <string>
#include <imgui/imgui.h>

#include <features/esp/rig.h>
#include <sdk/math/math.h>

// Chams drawn on the GPU so they fit the body. Every body part is drawn as its own shape: exact
// boxes for R6 limbs and torsos (that is what they are), rounded pieces for R15 parts, and a
// round head. They go into an offscreen layer through the game's own view-projection matrix, with
// depth, so limbs stay separate and the nearest surface wins. A second pass adds the outline and
// the effects, which are painted in each player's own body space: they move and turn with them
// and never spill outside the silhouette.
namespace body_chams
{
	enum effect_t { effect_flat = 0, effect_water, effect_plasma, effect_hologram, effect_galaxy };

	struct frame_t
	{
		math::matrix4 view_projection;
		math::vector2 dims;           // Roblox's render size
		math::vector2 offset;         // where Roblox's client area sits on the overlay
		int effect{ effect_flat };
		float speed{ 1.0f };
		float scale{ 1.0f };
		float outline_width{ 1.5f };  // pixels
	};

	// Call once a frame before any ESP drawing: puts the chams layer at the bottom of `draw`.
	// Does nothing (and returns false) if the shaders couldn't be built.
	bool begin(ImDrawList* draw);

	// Queues one body. Colours are RGBA 0..1, alpha already including fades.
	void add(const rig::body_t& body, const float fill[4], const float outline[4]);

	// Renders everything queued since begin() into the layer.
	void end(const frame_t& frame);

	// A separate small layer for the menu's preview: renders `bodies` with `frame` (offset ignored,
	// dims = the texture size) and returns the texture, or 0 if the shaders aren't available.
	ImTextureID render_preview(const rig::body_t* bodies, int count, const float fill[4], const float outline[4], const frame_t& frame);

	// Why the layer isn't working, or "" when it is (for the menu).
	const std::string& error();
}
