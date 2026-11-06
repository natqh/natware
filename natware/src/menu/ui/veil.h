#pragma once

#include <d3d11.h>
#include <imgui/imgui.h>

// Menu background: React Bits "Dark Veil" (a CPPN field + glow/tone-mapping composite),
// ported from its WebGL2 shaders to two D3D11 pixel-shader passes, plus the "Glass Surface"
// panes that bend it. Each window that renders a veil gets its own, keyed by its root window.
namespace ui::veil
{
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);

    // Renders the current window's veil at `origin` (screen px) of `size` and returns its
    // texture, or 0 if the shaders aren't available. The pointer swirl reads ImGui's mouse.
    ImTextureID Render(ImVec2 origin, ImVec2 size);

    // A veil for a pane outside the menu (the watermark): the screen rect origin..origin+size
    // shows the part of a virtual panel `view` px large whose top-left is at `view_offset`
    // within it, so the pattern keeps the menu's scale. No pointer swirl.
    ImTextureID RenderRegion(ImVec2 origin, ImVec2 size, ImVec2 view, ImVec2 view_offset);

    // React Bits "Glass Surface" over the current window's veil: a rounded pane that refracts it
    // at its edges (per-channel, so the bend fringes). `frost` is its backgroundOpacity (black,
    // as in the dark scheme); `distortion` its distortionScale. Falls back to a flat translucent
    // fill when this window has no veil this frame.
    // No refraction by default now: the backdrop is a character grid, and bending it smears the
    // glyphs into bands. A pane is a clean cut-out of the field under its own surface.
    void Glass(ImDrawList* dl, ImVec2 min, ImVec2 max, float radius, float frost = 0.0f, float distortion = 0.0f);

    // Group opacity: marks `list` to be drawn at `alpha` as part of one flattened layer this
    // frame, so overlapping panes fade together. RenderDrawData honours it.
    void FadeList(const ImDrawList* list, float alpha);

    // Draws ImGui's frame into `target` (already cleared), routing faded lists through an
    // offscreen layer. Use in place of ImGui_ImplDX11_RenderDrawData.
    void RenderDrawData(ImDrawData* draw_data, ID3D11RenderTargetView* target);
}
