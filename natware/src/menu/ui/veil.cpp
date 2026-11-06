#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "veil.h"

#include <imgui/imgui_internal.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // React Bits "Gradient ASCII" (shape: horizon), in the launcher's colours: a sun low in the
    // frame drifting sideways under a domain-warped haze, drawn as a grid of pixel glyphs picked
    // by brightness from ' =+xX0S8#@' and coloured through a warm gradient; the glyphs scramble
    // under the cursor. GradientASCII.tsx defaults, where they apply.
    constexpr float    kSpeed      = 0.5f;     // time += dt * speed * 0.5, as in the component
    constexpr float    kFlow       = 0.5f;
    constexpr float    kScale      = 1.0f;
    constexpr float    kGlow       = 0.12f;
    constexpr float    kGrain      = 0.03f;
    constexpr float    kDensity    = 0.65f;
    constexpr float    kOpacity    = 0.42f;    // the component's 0.85 is for a page; behind a menu the glyphs stay a texture
    constexpr float    kFlicker    = 0.2f;
    constexpr float    kRadius     = 0.1f;     // scrambleRadius, as a fraction of the panel's height
    constexpr float    kQuality    = 0.5f;     // field resolution relative to the panel
    constexpr float    kIntro      = 1.2f;     // seconds (the component's 2.4 felt slow on a menu that opens often)
    constexpr float    kMouse      = 1.0f;
    constexpr float    kCellW      = 12.0f;    // a 5x7 glyph at 2 px a bit, with a pixel of air around it
    constexpr float    kCellH      = 16.0f;
    constexpr unsigned kBackground = 0x0F0B09; // the warm near-black the whole menu sits on
    // The gradient, from the launcher's picture: ember, amber, sand, and sunlit sand at the core.
    constexpr unsigned kStops[4]   = { 0x2A1408, 0xC9792B, 0xF0C38A, 0xFBE9CB };

    const char kHeader[] = R"(
cbuffer FieldParams : register(b0)
{
    float2 uView; float2 uScale;
    float uZoom; float uTime; float uFlow; float uWarp;
    float2 uTurn; float uUnfold; float uDither;
    float4 uPointer;
    float3 uSeed; float uPadA;
    float2 uDrag; float2 uOffset;   // uOffset: where this target sits within the virtual panel
};

float4 vs_main(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// Value noise with the component's quintic fade.
float vnoise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash12(i), b = hash12(i + float2(1.0, 0.0)), c = hash12(i + float2(0.0, 1.0)), d = hash12(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
)";

    // The field: brightness 0..1 of the horizon glow at each point, at half resolution.
    const char kField[] = R"(
float4 ps_field(float4 pos : SV_Position) : SV_Target
{
    float2 css = pos.xy * uScale + uOffset;
    float2 uv = css / uView;
    float aspect = uView.x / uView.y;
    float2 p = float2(uv.x * aspect, uv.y);
    float t = uTime;

    // Domain warp: two noise samples drifting against each other (flow).
    float2 warp = float2(vnoise(p * 1.7 + float2(t * 0.3, 0.0)), vnoise(p * 1.7 + float2(0.0, -t * 0.24))) - 0.5;
    float2 q = p + warp * uFlow * 0.18;

    // Horizon: a sun sitting on the bottom edge that drifts sideways, a band along the
    // horizon, a haze. The top of the panel, where the titles are, stays dark.
    // Slightly bigger and higher than the original (the user's call; two larger steps were too
    // much). Original: sun y 0.98, d.y * 1.9, falloff 5.5, band at 0.92 * 6.
    float2 sun = float2(aspect * (0.5 + 0.22 * sin(t * 0.13)), 0.975);
    float2 d = q - sun;
    d.y *= 1.78;
    float glow = exp(-dot(d, d) * 5.0 / uZoom);
    float dy = (q.y - 0.9) * 5.6;
    float band = exp(-dy * dy) * 0.45;   // not pow(): a negative base is NaN in HLSL
    float haze = vnoise(q * 3.0 + t * 0.07) * 0.18;
    float v = saturate((glow * 0.95 + band * (0.3 + glow) + haze * glow) * 0.85 * uUnfold);
    return float4(v, 0.0, 0.0, 1.0);
}
)";

    const char kComposite[] = R"(
Texture2D uField : register(t0);
SamplerState uSampler : register(s0);

cbuffer CompositeParams : register(b0)
{
    float2 uResolution; float2 uTexel;
    float2 uCell; float2 uCursor;   // (uPointer is the field's; cbuffer names share one scope)
    float uPointerStrength; float uPointerRadius; float uDensity; float uOpacity;
    float uGlow; float uGrain; float uClock; float uFlicker;
    float4 uStops[4];
    float4 uSky;   // rgb: what the panel sits on; w: gradientOpacity
};

// ' =+xX0S8#@' as 5x7 bitmaps: x holds rows 0..3 (5 bits a row, bit 4 the left column), y rows 4..6.
static const uint2 kGlyphs[10] = {
    uint2(0u, 0u), uint2(31744u, 31u), uint2(1020032u, 132u), uint2(141856u, 554u), uint2(141873u, 17962u),
    uint2(708142u, 14905u), uint2(475663u, 30753u), uint2(476718u, 14897u), uint2(359754u, 10591u), uint2(712238u, 14871u)
};

float3 tone(float t)
{
    float x = smoothstep(0.3, 1.0, t) * 3.0;
    int i = (int)floor(x);
    return lerp(uStops[i].rgb, uStops[min(i + 1, 3)].rgb, frac(x));
}

float glyph(int index, float2 px)
{
    // The glyph sits 2 px a bit with a pixel of air: crisp, like the launcher's picture.
    float2 g = (px - 1.0) * 0.5;
    if (g.x < 0.0 || g.y < 0.0 || g.x >= 5.0 || g.y >= 7.0)
        return 0.0;
    int c = (int)g.x, r = (int)g.y;
    uint2 bits = kGlyphs[clamp(index, 0, 9)];
    uint row = r < 4 ? (bits.x >> (uint)(r * 5)) : (bits.y >> (uint)((r - 4) * 5));
    return (float)((row >> (uint)(4 - c)) & 1u);
}

float4 ps_composite(float4 pos : SV_Position) : SV_Target
{
    float2 uv = pos.xy / uResolution;
    float value = uField.SampleLevel(uSampler, uv, 0.0).x;
    float halo = uField.SampleLevel(uSampler, uv, 3.0).x * 0.6 + uField.SampleLevel(uSampler, uv, 4.0).x * 0.4;

    // The glyph for this cell, from the field at the cell's centre.
    float2 cell = floor(pos.xy / uCell);
    float2 centre = (cell + 0.5) * uCell;
    float cv = uField.SampleLevel(uSampler, centre / uResolution, 0.0).x;
    float tick = floor(uClock * 18.0);   // the component's 9 Hz glyph clock (uClock runs at half speed)
    float wobble = (hash12(cell * 2.31 + tick * 17.7) - 0.5) * uFlicker * 0.4;
    float t = saturate(pow(cv, 0.75) + wobble);
    int index = (int)floor(t * 9.0 + 0.5);

    // Scramble under the cursor: a random glyph, re-rolled 20 times a second.
    float2 dp = (centre - uCursor) / uResolution.y;
    float scramble = uPointerStrength * exp(-dot(dp, dp) / (uPointerRadius * uPointerRadius));
    float tick20 = floor(uClock * 40.0);
    if (scramble > hash12(cell * 7.7 + tick20 * 3.1))
        index = (int)(hash12(cell * 5.3 + tick20 * 13.7) * 9.999);

    float present = step(hash12(cell * 1.17 + 3.3), uDensity * lerp(1.0, 0.3 + t * 1.1, 0.55));
    float cutoff = smoothstep(0.03, 0.1, cv);
    float cover = glyph(index, pos.xy - cell * uCell);
    float alpha = uOpacity * present * cutoff * cover * lerp(1.0, 0.35 + 0.65 * cv, 0.55);

    // Dark composite: the soft gradient under the glyphs, the glyphs a step brighter, a halo.
    float3 color = uSky.rgb + tone(value) * smoothstep(0.0, 0.38, value) * uSky.w;
    color += tone(halo) * halo * uGlow;
    color = lerp(color, tone(saturate(cv + 0.25)), alpha);
    float grain = (hash12(pos.xy + frac(uClock * 7.31) * 113.0) - 0.5) * uGrain;
    return float4(saturate(color + grain), 1.0);
}
)";


    // GlassSurface.jsx in its refractive (Chromium SVG) mode. The component renders an SVG
    // displacement map and runs the backdrop through feDisplacementMap once per colour channel;
    // here the map is evaluated analytically. Checked against Chrome's own raster of the map:
    //  - black, then red = (1-u)^2 across and blue = v^2 down (SVG gradients from transparent
    //    interpolate unpremultiplied, hence the squares; blue is unused by the default x=R, y=G);
    //  - a 50% grey rect inset by min(w,h) * borderWidth / 2 at 93% opacity, blurred by `blur`,
    //    i.e. map = lerp(base, 0.5, 0.93 * gaussian_box).
    // Then the dark-scheme box-shadows: three faint inset ink shadows, and in place of its two
    // light inset rims a single thin dark one (the user wanted a slightly darker edge).
    const char kGlass[] = R"(
Texture2D uBackdrop : register(t0);
SamplerState uSampler : register(s0);

cbuffer GlassParams : register(b0)
{
    float4 uRect;      // min.xy, max.xy in ImGui coordinates
    float4 uVeil;      // veil origin.xy, 1 / veil size.xy
    float4 uDisplay;   // display origin.xy, 1 / framebuffer scale.xy
    float uRadius; float uEdge; float uBlur; float uMapOpacity;
    float3 uScale; float uFrost;
};

float erf_approx(float x)
{
    float x2 = x * x;
    float r = sqrt(1.0 - exp(-x2 * (1.2732395 + 0.147 * x2) / (1.0 + 0.147 * x2)));
    return x < 0.0 ? -r : r;
}

float phi(float z) { return 0.5 + 0.5 * erf_approx(z * 0.70710678); }

float rounded_box(float2 p, float2 half_size, float r)
{
    float2 q = abs(p) - half_size + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// A box convolved with a Gaussian (separable, so exact for square corners).
float blurred_box(float2 p, float2 lo, float2 hi, float sigma)
{
    float2 a = (p - lo) / sigma, b = (p - hi) / sigma;
    return (phi(a.x) - phi(b.x)) * (phi(a.y) - phi(b.y));
}

// An inset box-shadow's coverage: outside the padding box shrunk by `spread` and moved by
// `offset`, blurred (CSS blur radius = 2 sigma).
float inset_shadow(float2 p, float2 half_size, float r, float2 offset, float spread, float blur)
{
    return phi((rounded_box(p - half_size - offset, half_size, r) + spread) / (blur * 0.5));
}

float4 ps_glass(float4 pos : SV_POSITION, float4 col : COLOR0, float2 uv : TEXCOORD0) : SV_Target
{
    float2 screen = pos.xy * uDisplay.zw + uDisplay.xy;
    float2 size = uRect.zw - uRect.xy;
    float2 half_size = size * 0.5;
    float2 p = screen - uRect.xy;
    float r = min(uRadius, min(half_size.x, half_size.y));
    float coverage = saturate(0.5 - rounded_box(p - half_size, half_size, r));
    clip(coverage - 0.001);

    float2 t = saturate(p / size);
    float3 map = float3((1.0 - t.x) * (1.0 - t.x), 0.0, t.y * t.y) * coverage;
    map = lerp(map, 0.5, uMapOpacity * blurred_box(p, uEdge.xx, size - uEdge, uBlur));

    // feDisplacementMap: P(x + scale * (R - 0.5), y + scale * (G - 0.5)), one scale per channel,
    // and the three single-channel results screen-blended back into one colour.
    float2 shift = map.rg - 0.5;
    float2 base = (screen - uVeil.xy) * uVeil.zw;
    float3 color;
    color.r = uBackdrop.SampleLevel(uSampler, base + shift * uScale.r * uVeil.zw, 0.0).r;
    color.g = uBackdrop.SampleLevel(uSampler, base + shift * uScale.g * uVeil.zw, 0.0).g;
    color.b = uBackdrop.SampleLevel(uSampler, base + shift * uScale.b * uVeil.zw, 0.0).b;

    // background: hsl(0 0% 0% / backgroundOpacity)
    color = lerp(color, 0.0, uFrost);

    // box-shadows, bottom-most first (the component's two white inset rims are left out on purpose)
    float3 ink = float3(17.0, 17.0, 26.0) / 255.0;
    color = lerp(color, ink, 0.05 * inset_shadow(p, half_size, r, float2(0.0, 16.0), 0.0, 56.0));
    color = lerp(color, ink, 0.05 * inset_shadow(p, half_size, r, float2(0.0, 8.0), 0.0, 24.0));
    color = lerp(color, ink, 0.05 * inset_shadow(p, half_size, r, float2(0.0, 4.0), 0.0, 16.0));

    // A thin dark rim where the component draws its light one (0 0 2px 1px): the user wanted
    // the edge a little darker than the pane, not an outline in a colour.
    color = lerp(color, 0.0, 0.5 * inset_shadow(p, half_size, r, float2(0.0, 0.0), 1.0, 1.5));
    return float4(color, coverage * col.a);
}
)";

    // Group opacity: a faded part of the menu is drawn into an offscreen layer (premultiplied,
    // as ImGui's blending leaves it over transparent black) and blended onto the frame once,
    // so overlapping panes fade as one picture instead of each showing through the next.
    const char kLayer[] = R"(
Texture2D uLayer : register(t0);
SamplerState uSampler : register(s0);
cbuffer LayerParams : register(b0) { float4 uOpacity; };

float4 vs_layer(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 ps_layer(float4 pos : SV_Position) : SV_Target
{
    return uLayer.Load(int3(pos.xy, 0)) * uOpacity.x;
}
)";

    // GlassSurface.jsx defaults.
    constexpr float kGlassBorderWidth = 0.07f;
    constexpr float kGlassBlur        = 11.0f;
    constexpr float kGlassOpacity     = 0.93f;
    constexpr float kGlassOffsets[3]  = { 0.0f, 10.0f, 20.0f };   // red/green/blue offsets

    struct GlassParams
    {
        float rect[4];
        float veil[4];
        float display[4];
        float radius, edge, blur, map_opacity;
        float scale[3], frost;
    };
    static_assert(sizeof(GlassParams) % 16 == 0, "16-byte constant buffer");

    struct FieldParams
    {
        float view[2], scale[2];
        float zoom, time, flow, warp;
        float turn[2], unfold, dither;
        float pointer[4];
        float seed[3], pad_a;
        float drag[2], offset[2];
    };

    struct CompositeParams
    {
        float resolution[2], texel[2];
        float cell[2], pointer[2];
        float pointer_strength, pointer_radius, density, opacity;
        float glow, grain, clock, flicker;
        float stops[4][4];
        float sky[4];
    };
    static_assert(sizeof(FieldParams) % 16 == 0 && sizeof(CompositeParams) % 16 == 0, "16-byte constant buffers");

    ID3D11Device*             g_device = nullptr;
    ID3D11DeviceContext*      g_context = nullptr;
    ID3D11VertexShader*       g_vs = nullptr;
    ID3D11PixelShader*        g_ps_field = nullptr;
    ID3D11PixelShader*        g_ps_composite = nullptr;
    ID3D11Buffer*             g_field_cb = nullptr;
    ID3D11Buffer*             g_composite_cb = nullptr;
    ID3D11PixelShader*        g_ps_glass = nullptr;
    ID3D11Buffer*             g_glass_cb = nullptr;
    ID3D11SamplerState*       g_sampler = nullptr;
    ID3D11RasterizerState*    g_raster = nullptr;

    // Offscreen layer for group fades.
    ID3D11VertexShader*       g_vs_layer = nullptr;
    ID3D11PixelShader*        g_ps_layer = nullptr;
    ID3D11Buffer*             g_layer_cb = nullptr;
    ID3D11BlendState*         g_blend_premultiplied = nullptr;
    ID3D11Texture2D*          g_layer = nullptr;
    ID3D11RenderTargetView*   g_layer_rtv = nullptr;
    ID3D11ShaderResourceView* g_layer_srv = nullptr;
    UINT g_layer_w = 0, g_layer_h = 0;

    struct FadeEntry { const ImDrawList* list; float alpha; };
    std::vector<FadeEntry> g_fades;
    int g_fades_frame = -1;

    // Animation state, as in DarkVeil.jsx's frame().
    struct State
    {
        float clock = 0.0f, intro = 0.0f, touch = 0.0f;
        float x = 0.0f, y = 0.0f, drag_x = 0.0f, drag_y = 0.0f;
        float light_x = 0.5f, light_y = 0.15f;
        int last_frame = -10;
    };

    // One veil per window that draws one (the menu, each tool window, the watermark), keyed by
    // the root window, so the glass in a window bends that window's own veil.
    struct Instance
    {
        ID3D11Texture2D*          field = nullptr;
        ID3D11RenderTargetView*   field_rtv = nullptr;
        ID3D11ShaderResourceView* field_srv = nullptr;
        ID3D11Texture2D*          out = nullptr;
        ID3D11RenderTargetView*   out_rtv = nullptr;
        ID3D11ShaderResourceView* out_srv = nullptr;
        UINT w = 0, h = 0;
        ImVec2 origin, size;   // where this frame's veil sits on screen
        State state;
    };
    std::unordered_map<ImGuiID, Instance> g_instances;

    ImGuiID InstanceKey()
    {
        ImGuiWindow* w = ImGui::GetCurrentWindowRead();
        return w ? w->RootWindow->ID : 0;
    }

    template <typename T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

    ID3DBlob* Compile(const std::string& src, const char* entry, const char* profile)
    {
        ID3DBlob* code = nullptr;
        ID3DBlob* errors = nullptr;
        const HRESULT hr = D3DCompile(src.data(), src.size(), "dark_veil", nullptr, nullptr, entry, profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(hr))
            printf("dark veil: %s failed to compile: %s\n", entry, errors ? (const char*)errors->GetBufferPointer() : "unknown error");
        Release(errors);
        return code;
    }

    void Linear(float out[3], unsigned rgb)
    {
        out[0] = powf(((rgb >> 16) & 0xFF) / 255.0f, 2.2f);
        out[1] = powf(((rgb >> 8) & 0xFF) / 255.0f, 2.2f);
        out[2] = powf((rgb & 0xFF) / 255.0f, 2.2f);
    }

    bool MakeTarget(UINT w, UINT h, DXGI_FORMAT format, bool mips, ID3D11Texture2D*& tex, ID3D11RenderTargetView*& rtv, ID3D11ShaderResourceView*& srv)
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = mips ? 0 : 1;
        td.ArraySize = 1;
        td.Format = format;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        return SUCCEEDED(g_device->CreateTexture2D(&td, nullptr, &tex))
            && SUCCEEDED(g_device->CreateRenderTargetView(tex, nullptr, &rtv))
            && SUCCEEDED(g_device->CreateShaderResourceView(tex, nullptr, &srv));
    }

    bool EnsureTargets(Instance& in, UINT w, UINT h)
    {
        if (in.out && w == in.w && h == in.h)
            return true;
        Release(in.field_srv); Release(in.field_rtv); Release(in.field);
        Release(in.out_srv);   Release(in.out_rtv);   Release(in.out);

        const UINT fw = (UINT)std::max(1.0f, roundf(w * kQuality));
        const UINT fh = (UINT)std::max(1.0f, roundf(h * kQuality));
        if (!MakeTarget(fw, fh, DXGI_FORMAT_R16G16B16A16_FLOAT, true, in.field, in.field_rtv, in.field_srv)
            || !MakeTarget(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false, in.out, in.out_rtv, in.out_srv))
        {
            Release(in.field_srv); Release(in.field_rtv); Release(in.field);
            Release(in.out_srv);   Release(in.out_rtv);   Release(in.out);
            return false;
        }
        in.w = w;
        in.h = h;
        return true;
    }

    template <typename T>
    void Upload(ID3D11Buffer* buffer, const T& data)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(g_context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &data, sizeof(T));
            g_context->Unmap(buffer, 0);
        }
    }

    ID3D11Buffer* MakeConstants(UINT size)
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = size;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* b = nullptr;
        g_device->CreateBuffer(&bd, nullptr, &b);
        return b;
    }

    float Clamp(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

    // ImDrawList callback: swaps the ImGui backend's pixel shader for the glass one. Its vertex
    // shader keeps running, and the image command that follows binds the veil as t0.
    void BindPane(const ImDrawCmd* cmd, ID3D11PixelShader* ps)
    {
        Upload(g_glass_cb, *static_cast<const GlassParams*>(cmd->UserCallbackData));
        g_context->PSSetShader(ps, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_glass_cb);
        g_context->PSSetSamplers(0, 1, &g_sampler);
    }
    void BindGlass(const ImDrawList*, const ImDrawCmd* cmd) { BindPane(cmd, g_ps_glass); }

    GlassParams PaneParams(ImVec2 min, ImVec2 max, float radius)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 display = ImGui::GetMainViewport()->Pos;
        GlassParams p{};
        p.rect[0] = min.x;
        p.rect[1] = min.y;
        p.rect[2] = max.x;
        p.rect[3] = max.y;
        p.display[0] = display.x;
        p.display[1] = display.y;
        p.display[2] = 1.0f / std::max(io.DisplayFramebufferScale.x, 0.01f);
        p.display[3] = 1.0f / std::max(io.DisplayFramebufferScale.y, 0.01f);
        p.radius = radius;
        return p;
    }

    int StyleAlpha() { return (int)(Clamp(ImGui::GetStyle().Alpha, 0.0f, 1.0f) * 255.0f + 0.5f); }
}

bool ui::veil::Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
    g_device = device;
    g_context = context;

    std::string src = kHeader;
    src += kField;
    src += kComposite;

    ID3DBlob* vs = Compile(src, "vs_main", "vs_4_0");
    ID3DBlob* pf = Compile(src, "ps_field", "ps_4_0");
    ID3DBlob* pc = Compile(src, "ps_composite", "ps_4_0");
    if (vs && pf && pc)
    {
        device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs);
        device->CreatePixelShader(pf->GetBufferPointer(), pf->GetBufferSize(), nullptr, &g_ps_field);
        device->CreatePixelShader(pc->GetBufferPointer(), pc->GetBufferSize(), nullptr, &g_ps_composite);
    }
    Release(vs);
    Release(pf);
    Release(pc);

    if (ID3DBlob* pg = Compile(kGlass, "ps_glass", "ps_4_0"))
    {
        device->CreatePixelShader(pg->GetBufferPointer(), pg->GetBufferSize(), nullptr, &g_ps_glass);
        Release(pg);
    }
    ID3DBlob* vl = Compile(kLayer, "vs_layer", "vs_4_0");
    ID3DBlob* pl = Compile(kLayer, "ps_layer", "ps_4_0");
    if (vl && pl)
    {
        device->CreateVertexShader(vl->GetBufferPointer(), vl->GetBufferSize(), nullptr, &g_vs_layer);
        device->CreatePixelShader(pl->GetBufferPointer(), pl->GetBufferSize(), nullptr, &g_ps_layer);
    }
    Release(vl);
    Release(pl);

    g_field_cb = MakeConstants(sizeof(FieldParams));
    g_composite_cb = MakeConstants(sizeof(CompositeParams));
    g_glass_cb = MakeConstants(sizeof(GlassParams));
    g_layer_cb = MakeConstants(16);

    // Premultiplied "over", for the layer: it already carries its alpha in its colour.
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bd, &g_blend_premultiplied);

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sd, &g_sampler);

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rd, &g_raster);

    return g_vs && g_ps_field && g_ps_composite && g_field_cb && g_composite_cb && g_sampler && g_raster;
}

// Renders `in`'s veil over the screen rect origin..origin+size. That rect shows the part of a
// virtual panel `view` px large whose top-left sits at `offset` within it (the menu and the tool
// windows are their own panel; the watermark shows a cut-out of a menu-sized one).
static ImTextureID RenderInstance(Instance& in, ImVec2 origin, ImVec2 size, ImVec2 view, ImVec2 offset, bool interactive)
{
    if (!g_vs || !g_ps_field || !g_ps_composite || size.x < 1.0f || size.y < 1.0f)
        return (ImTextureID)0;

    const UINT w = (UINT)ceilf(size.x), h = (UINT)ceilf(size.y);
    if (!EnsureTargets(in, w, h))
        return (ImTextureID)0;
    in.origin = origin;
    in.size = ImVec2((float)w, (float)h);

    // ---- animation (DarkVeil.jsx frame()) ----
    const ImGuiIO& io = ImGui::GetIO();
    State& s = in.state;
    const float dt = Clamp(io.DeltaTime, 0.0f, 0.05f);

    // A window isn't drawn while it's closed, so a gap means it was just reopened: replay the
    // intro, as the component does on mount.
    if (ImGui::GetFrameCount() > s.last_frame + 1)
        s.intro = 0.0f;
    s.last_frame = ImGui::GetFrameCount();

    s.clock += dt * kSpeed;
    s.intro = std::min(1.0f, s.intro + dt / kIntro);

    const bool valid = ImGui::IsMousePosValid();
    const float px = valid ? io.MousePos.x - origin.x : s.x;
    const float py = valid ? io.MousePos.y - origin.y : s.y;
    const bool engaged = interactive && valid && px >= 0.0f && py >= 0.0f && px <= size.x && py <= size.y;
    if (engaged && s.touch < 0.01f)
    {
        s.x = px;
        s.y = py;
    }
    // DarkVeil.jsx's follow rates, slowed ~40% to match the menu's longer, softer motion.
    const float follow = 1.0f - expf(-dt * 5.5f);
    const float prev_x = s.x, prev_y = s.y;
    s.x += (px - s.x) * follow;
    s.y += (py - s.y) * follow;
    const float vel_x = (s.x - prev_x) / std::max(dt, 0.001f);
    const float vel_y = (s.y - prev_y) / std::max(dt, 0.001f);
    const float drag_blend = 1.0f - expf(-dt * 3.0f);
    s.drag_x += (Clamp(vel_x * 0.12f, -120.0f, 120.0f) - s.drag_x) * drag_blend;
    s.drag_y += (Clamp(-vel_y * 0.12f, -120.0f, 120.0f) - s.drag_y) * drag_blend;
    s.touch += ((engaged ? 1.0f : 0.0f) - s.touch) * (1.0f - expf(-dt * (engaged ? 1.8f : 1.0f)));
    const float light_goal_x = engaged ? px / size.x : 0.5f;
    const float light_goal_y = engaged ? py / size.y : 0.15f;
    const float light_follow = 1.0f - expf(-dt * 2.4f);
    s.light_x += (light_goal_x - s.light_x) * light_follow;
    s.light_y += (light_goal_y - s.light_y) * light_follow;

    const float i = s.intro;
    const float presence = i * i * i * (i * (i * 6.0f - 15.0f) + 10.0f);

    D3D11_TEXTURE2D_DESC fd{};
    in.field->GetDesc(&fd);

    FieldParams fp{};
    fp.view[0] = view.x;
    fp.view[1] = view.y;
    fp.offset[0] = offset.x;
    fp.offset[1] = offset.y;
    fp.scale[0] = size.x / fd.Width;
    fp.scale[1] = size.y / fd.Height;
    fp.zoom = kScale;
    fp.time = s.clock;
    fp.flow = kFlow;
    fp.warp = 0.0f;
    fp.turn[0] = 1.0f;
    fp.turn[1] = 0.0f;
    fp.unfold = presence;   // the intro: the glow rises over the first seconds
    fp.dither = 0.0f;   // the field target is float, as with EXT_color_buffer_float
    fp.pointer[0] = s.x + offset.x;
    fp.pointer[1] = s.y + offset.y;
    fp.pointer[2] = s.touch * kMouse * 0.9f;
    fp.pointer[3] = 0.42f;
    fp.seed[0] = 0.3948333106474662f;
    fp.seed[1] = 0.36f;
    fp.seed[2] = 0.14f;
    fp.drag[0] = s.drag_x * 2.0f / view.y;
    fp.drag[1] = s.drag_y * 2.0f / view.y;
    Upload(g_field_cb, fp);

    CompositeParams cp{};
    cp.resolution[0] = (float)w;
    cp.resolution[1] = (float)h;
    cp.texel[0] = 1.0f / fd.Width;
    cp.texel[1] = 1.0f / fd.Height;
    cp.cell[0] = kCellW;
    cp.cell[1] = kCellH;
    cp.pointer[0] = s.x;
    cp.pointer[1] = s.y;
    cp.pointer_strength = s.touch * kMouse;
    cp.pointer_radius = kRadius;
    cp.density = kDensity;
    cp.opacity = kOpacity;
    cp.glow = kGlow;
    cp.grain = kGrain;
    cp.clock = fmodf(s.clock, 1000.0f);
    cp.flicker = kFlicker;
    for (int k = 0; k < 4; k++)
    {
        cp.stops[k][0] = ((kStops[k] >> 16) & 0xFF) / 255.0f;
        cp.stops[k][1] = ((kStops[k] >> 8) & 0xFF) / 255.0f;
        cp.stops[k][2] = (kStops[k] & 0xFF) / 255.0f;
        cp.stops[k][3] = 1.0f;
    }
    cp.sky[0] = ((kBackground >> 16) & 0xFF) / 255.0f;
    cp.sky[1] = ((kBackground >> 8) & 0xFF) / 255.0f;
    cp.sky[2] = (kBackground & 0xFF) / 255.0f;
    cp.sky[3] = 0.26f;   // gradientOpacity, held down so the menu's text stays the brightest thing
    Upload(g_composite_cb, cp);

    // ---- passes ----
    // No state is restored: this runs before the frame's ImGui draw, and the ImGui DX11
    // backend sets every piece of pipeline state it uses before it draws.
    ID3D11RenderTargetView* none_rtv = nullptr;
    ID3D11ShaderResourceView* none_srv = nullptr;
    g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(nullptr, 0);
    g_context->RSSetState(g_raster);
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_vs, nullptr, 0);
    g_context->GSSetShader(nullptr, nullptr, 0);

    const D3D11_VIEWPORT field_vp{ 0.0f, 0.0f, (float)fd.Width, (float)fd.Height, 0.0f, 1.0f };
    g_context->PSSetShaderResources(0, 1, &none_srv);
    g_context->OMSetRenderTargets(1, &in.field_rtv, nullptr);
    g_context->RSSetViewports(1, &field_vp);
    g_context->PSSetShader(g_ps_field, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_field_cb);
    g_context->Draw(3, 0);

    g_context->OMSetRenderTargets(1, &none_rtv, nullptr);
    g_context->GenerateMips(in.field_srv);

    const D3D11_VIEWPORT out_vp{ 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    g_context->OMSetRenderTargets(1, &in.out_rtv, nullptr);
    g_context->RSSetViewports(1, &out_vp);
    g_context->PSSetShader(g_ps_composite, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_composite_cb);
    g_context->PSSetShaderResources(0, 1, &in.field_srv);
    g_context->PSSetSamplers(0, 1, &g_sampler);
    g_context->Draw(3, 0);

    g_context->OMSetRenderTargets(1, &none_rtv, nullptr);
    g_context->PSSetShaderResources(0, 1, &none_srv);
    return (ImTextureID)(intptr_t)in.out_srv;
}

ImTextureID ui::veil::Render(ImVec2 origin, ImVec2 size)
{
    return RenderInstance(g_instances[InstanceKey()], origin, size, size, ImVec2(0.0f, 0.0f), true);
}

ImTextureID ui::veil::RenderRegion(ImVec2 origin, ImVec2 size, ImVec2 view, ImVec2 view_offset)
{
    return RenderInstance(g_instances[InstanceKey()], origin, size, view, view_offset, false);
}

void ui::veil::Glass(ImDrawList* dl, ImVec2 min, ImVec2 max, float radius, float frost, float distortion)
{
    // Only over a veil this window rendered this frame (Render stamps last_frame).
    const auto it = g_instances.find(InstanceKey());
    if (!g_ps_glass || !g_glass_cb || it == g_instances.end() || !it->second.out_srv
        || it->second.state.last_frame != ImGui::GetFrameCount())
    {
        dl->AddRectFilled(min, max, IM_COL32(16, 12, 9, 205), radius);
        return;
    }
    const Instance& in = it->second;

    GlassParams p = PaneParams(min, max, radius);
    p.veil[0] = in.origin.x;
    p.veil[1] = in.origin.y;
    p.veil[2] = 1.0f / in.size.x;
    p.veil[3] = 1.0f / in.size.y;
    p.edge = std::min(max.x - min.x, max.y - min.y) * kGlassBorderWidth * 0.5f;
    p.blur = kGlassBlur;
    p.map_opacity = kGlassOpacity;
    for (int c = 0; c < 3; c++)
        p.scale[c] = distortion + kGlassOffsets[c];
    p.frost = frost;

    // The quad runs a pixel past the pane so the shader can anti-alias its edge; its vertex
    // alpha carries the style alpha (page fades).
    dl->AddCallback(BindGlass, &p, sizeof(p));
    dl->AddImage((ImTextureID)(intptr_t)in.out_srv, ImVec2(min.x - 1.0f, min.y - 1.0f), ImVec2(max.x + 1.0f, max.y + 1.0f),
        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, StyleAlpha()));
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void ui::veil::FadeList(const ImDrawList* list, float alpha)
{
    if (g_fades_frame != ImGui::GetFrameCount())
    {
        g_fades.clear();
        g_fades_frame = ImGui::GetFrameCount();
    }
    g_fades.push_back({ list, alpha });
}

namespace
{
    bool EnsureLayer(UINT w, UINT h)
    {
        if (g_layer && w == g_layer_w && h == g_layer_h)
            return true;
        Release(g_layer_srv); Release(g_layer_rtv); Release(g_layer);
        if (!MakeTarget(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false, g_layer, g_layer_rtv, g_layer_srv))
        {
            Release(g_layer_srv); Release(g_layer_rtv); Release(g_layer);
            return false;
        }
        g_layer_w = w;
        g_layer_h = h;
        return true;
    }

    float FadeOf(const ImDrawList* list)
    {
        if (g_fades_frame != ImGui::GetFrameCount())
            return 1.0f;
        for (const FadeEntry& f : g_fades)
            if (f.list == list)
                return f.alpha;
        return 1.0f;
    }
}

void ui::veil::RenderDrawData(ImDrawData* draw_data, ID3D11RenderTargetView* target)
{
    if (!g_context || !draw_data || draw_data->CmdLists.Size == 0)
    {
        if (draw_data)
            ImGui_ImplDX11_RenderDrawData(draw_data);
        return;
    }

    // Split the lists into runs of equal opacity, in draw order. Fully opaque runs draw straight
    // to the target; faded runs go through the layer.
    const UINT fw = (UINT)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x + 0.5f);
    const UINT fh = (UINT)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y + 0.5f);
    const bool layer_ok = g_vs_layer && g_ps_layer && g_layer_cb && g_blend_premultiplied && fw > 0 && fh > 0 && EnsureLayer(fw, fh);

    ImDrawData part = *draw_data;
    bool first = true;
    int i = 0;
    while (i < draw_data->CmdLists.Size)
    {
        float alpha = FadeOf(draw_data->CmdLists[i]);
        if (!layer_ok || alpha >= 0.999f)
            alpha = 1.0f;
        part.CmdLists.resize(0);
        part.TotalVtxCount = part.TotalIdxCount = 0;
        for (; i < draw_data->CmdLists.Size; i++)
        {
            ImDrawList* list = draw_data->CmdLists[i];
            const float a = (!layer_ok || FadeOf(list) >= 0.999f) ? 1.0f : FadeOf(list);
            if (part.CmdLists.Size > 0 && fabsf(a - alpha) > 0.0005f)
                break;
            part.CmdLists.push_back(list);
            part.TotalVtxCount += list->VtxBuffer.Size;
            part.TotalIdxCount += list->IdxBuffer.Size;
        }
        part.CmdListsCount = part.CmdLists.Size;
        part.Textures = first ? draw_data->Textures : nullptr;   // atlas uploads happen once
        first = false;

        if (alpha >= 1.0f)
        {
            g_context->OMSetRenderTargets(1, &target, nullptr);
            ImGui_ImplDX11_RenderDrawData(&part);
            continue;
        }

        const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_context->ClearRenderTargetView(g_layer_rtv, clear);
        g_context->OMSetRenderTargets(1, &g_layer_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(&part);

        const float opacity[4] = { alpha, 0.0f, 0.0f, 0.0f };
        Upload(g_layer_cb, opacity);
        ID3D11ShaderResourceView* none_srv = nullptr;
        const D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)fw, (float)fh, 0.0f, 1.0f };
        g_context->OMSetRenderTargets(1, &target, nullptr);
        g_context->OMSetBlendState(g_blend_premultiplied, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(nullptr, 0);
        g_context->RSSetState(g_raster);
        g_context->RSSetViewports(1, &vp);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_vs_layer, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(g_ps_layer, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_layer_cb);
        g_context->PSSetShaderResources(0, 1, &g_layer_srv);
        g_context->Draw(3, 0);
        g_context->PSSetShaderResources(0, 1, &none_srv);
    }
    g_context->OMSetRenderTargets(1, &target, nullptr);
}
