#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "body_chams.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <render/render.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	const char kShaders[] = R"(
cbuffer Frame : register(b0)
{
    row_major float4x4 uViewProj;
};

struct BodyIn
{
    float3 p    : POSITION;
    float4 w0   : WORLD0;
    float4 w1   : WORLD1;
    float4 w2   : WORLD2;
    float4 b0   : BODY0;
    float4 b1   : BODY1;
    float4 b2   : BODY2;
    float4 fill : FILL;
    float4 edge : EDGE;
};

struct BodyOut
{
    float4 pos : SV_Position;
    float3 body : TEXCOORD0;
    nointerpolation float4 fill : TEXCOORD1;
    nointerpolation float4 edge : TEXCOORD2;
};

BodyOut vs_body(BodyIn i)
{
    float4 p = float4(i.p, 1.0);
    float3 w = float3(dot(i.w0, p), dot(i.w1, p), dot(i.w2, p));
    float4 c = mul(uViewProj, float4(w, 1.0));
    BodyOut o;
    // Our own depth, reversed: z = 0.1 gives z/w = 0.1/w, big near and tiny far, where floats
    // are most precise (so far-away limbs don't flicker). It also clips whatever is nearer than
    // world_to_screen's 0.1 cut-off, or behind the camera.
    o.pos = float4(c.x, c.y, 0.1, c.w);
    o.body = float3(dot(i.b0, p), dot(i.b1, p), dot(i.b2, p));
    o.fill = i.fill;
    o.edge = i.edge;
    return o;
}

struct BodyTargets
{
    float4 fill : SV_Target0;   // fill colour, a = covered
    float4 body : SV_Target1;   // body-space position (studs), a = fill alpha
    float4 edge : SV_Target2;   // outline colour and alpha
};

BodyTargets ps_body(BodyOut i)
{
    BodyTargets o;
    o.fill = float4(i.fill.rgb, 1.0);
    o.body = float4(i.body, i.fill.a);
    o.edge = i.edge;
    return o;
}

Texture2D<float4> tFill : register(t0);
Texture2D<float4> tBody : register(t1);
Texture2D<float4> tEdge : register(t2);

cbuffer Comp : register(b0)
{
    float uTime;
    float uScale;
    float uEdge;
    int   uEffect;
};

float4 vs_full(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float hash31(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
}

float noise3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);
    float a = lerp(hash31(i), hash31(i + float3(1, 0, 0)), u.x);
    float b = lerp(hash31(i + float3(0, 1, 0)), hash31(i + float3(1, 1, 0)), u.x);
    float c = lerp(hash31(i + float3(0, 0, 1)), hash31(i + float3(1, 0, 1)), u.x);
    float d = lerp(hash31(i + float3(0, 1, 1)), hash31(i + float3(1, 1, 1)), u.x);
    return lerp(lerp(a, b, u.y), lerp(c, d, u.y), u.z);
}

float fbm(float3 p)
{
    float v = 0.0;
    float a = 0.5;
    [unroll] for (int k = 0; k < 4; k++)
    {
        v += a * noise3(p);
        p = p * 2.03 + 11.7;
        a *= 0.5;
    }
    return v;
}

float3 hue(float3 c, float turns)
{
    const float3 k = float3(0.57735, 0.57735, 0.57735);
    float a = turns * 6.2831853;
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

// The effect at one point of the body: colour, and how opaque (times the fill alpha).
float4 effect(float3 base, float3 P, float2 px, float rim, float t)
{
    float3 q = P * uScale;
    if (uEffect == 1)   // water: caustic light drifting over the body
    {
        float n = fbm(q * 1.4 + float3(0.0, t * 0.55, t * 0.3));
        float m = fbm(q * 2.7 - float3(t * 0.4, 0.0, t * 0.2));
        float caust = pow(saturate(1.0 - abs(sin((n + m * 0.5) * 9.0 + t * 0.8))), 5.0);
        float3 deep = base * 0.45;
        float3 light = lerp(base, float3(0.85, 0.97, 1.0), 0.55);
        return float4(lerp(deep, light, caust) + base * rim * 0.5, 0.65 + 0.35 * caust);
    }
    if (uEffect == 2)   // plasma: flowing bands that swing the hue around the fill colour
    {
        float v = sin(q.x * 3.1 + t) + sin(q.y * 2.3 - t * 1.3) + sin((q.x + q.z) * 2.2 + t * 0.7)
                + sin(length(q * 1.7) * 2.6 - t * 1.9);
        float band = 0.5 + 0.5 * sin(v * 3.14159);
        float3 c = lerp(hue(base, v * 0.05) * 0.55, saturate(hue(base, 0.12 + v * 0.06) * 1.25 + 0.15), band);
        return float4(c + base * rim * 0.4, 0.8 + 0.2 * band);
    }
    if (uEffect == 3)   // hologram: scan lines, a glowing rim, a band rising up the body, flicker
    {
        float scan = 0.5 + 0.5 * sin(px.y * 2.1 - t * 12.0);
        float rise = exp(-pow((frac(P.y * 0.22 - t * 0.35) - 0.5) * 7.0, 2.0));
        float flick = 0.88 + 0.12 * sin(t * 41.0) * sin(t * 13.3);
        float3 c = base * (0.45 + 0.35 * scan) + base * rim * 1.4 + 0.3 * rise;
        return float4(saturate(c), saturate(0.22 + 0.3 * scan + 0.9 * rim + 0.45 * rise) * flick);
    }
    if (uEffect == 4)   // galaxy: nebula clouds and twinkling stars inside the body
    {
        float neb = fbm(q * 0.9 + float3(t * 0.05, 0.0, -t * 0.03));
        float wisp = fbm(q * 2.4 - t * 0.04);
        float3 c = lerp(base * 0.15 + float3(0.02, 0.0, 0.05), base * 0.85 + hue(base, 0.22) * 0.35, smoothstep(0.35, 0.85, neb));
        c += hue(base, -0.18) * 0.35 * smoothstep(0.5, 0.9, wisp);
        float h = hash31(floor(q * 5.0));
        float3 f = frac(q * 5.0) - 0.5;
        float star = h > 0.9 ? smoothstep(0.16, 0.0, length(f)) * (0.55 + 0.45 * sin(t * 3.0 + h * 40.0)) : 0.0;
        return float4(c + star + base * rim * 0.35, 0.92);
    }
    return float4(base, 1.0);
}

static const int R = 5;   // how far (px) the outline, glow and rim look

float4 ps_comp(float4 pos : SV_Position) : SV_Target
{
    int2 px = int2(pos.xy);
    float4 f = tFill.Load(int3(px, 0));
    bool inside = f.a > 0.5;

    // Most pixels are far from any silhouette edge: two rings of taps tell.
    static const int2 probe[16] = {
        int2(-R, -R), int2(0, -R), int2(R, -R), int2(-R, 0), int2(R, 0), int2(-R, R), int2(0, R), int2(R, R),
        int2(-2, -2), int2(0, -2), int2(2, -2), int2(-2, 0), int2(2, 0), int2(-2, 2), int2(0, 2), int2(2, 2) };
    bool mixed = false;
    [unroll] for (int k = 0; k < 16; k++)
        mixed = mixed || ((tFill.Load(int3(px + probe[k], 0)).a > 0.5) != inside);

    float near_in = 99.0;
    float near_out = 99.0;
    int2 src = px;
    float outside = 0.0;
    float total = 0.0;
    if (mixed)
    {
        [loop] for (int y = -R; y <= R; y++)
        {
            [loop] for (int x = -R; x <= R; x++)
            {
                float d = length(float2(x, y));
                if (d > R + 0.5)
                    continue;
                bool in_n = tFill.Load(int3(px + int2(x, y), 0)).a > 0.5;
                total += 1.0;
                if (!in_n)
                {
                    outside += 1.0;
                    near_out = min(near_out, d);
                }
                else if (d < near_in)
                {
                    near_in = d;
                    src = px + int2(x, y);
                }
            }
        }
    }

    if (inside)
    {
        float4 b = tBody.Load(int3(px, 0));
        float4 edge = tEdge.Load(int3(px, 0));
        float rim = total > 0.0 ? saturate(outside / total * 3.0) : 0.0;
        float4 e = effect(f.rgb, b.xyz, pos.xy, rim, uTime);
        float fill_a = saturate(b.a * e.a);
        // The silhouette's last pixel is softened (it would be stair-stepped); with an outline
        // it blends into the outline colour instead of fading out.
        float aa = saturate(near_out - 0.35);
        bool outlined = uEdge > 0.0 && edge.a > 0.01;
        float3 rgb = outlined ? lerp(edge.rgb, e.rgb, aa) : e.rgb;
        float alpha = outlined ? lerp(edge.a, fill_a, aa) : fill_a * aa;
        return float4(rgb, alpha);
    }
    if (near_in < 98.0)
    {
        float4 edge = tEdge.Load(int3(src, 0));
        float stroke = saturate(uEdge + 0.5 - near_in);
        // A soft glow fades out past the outline.
        float glow = uEdge > 0.0 ? 0.32 * pow(saturate(1.0 - (near_in - uEdge) / (R - uEdge + 0.5)), 2.0) : 0.0;
        return float4(edge.rgb, edge.a * max(stroke, glow));
    }
    return float4(0.0, 0.0, 0.0, 0.0);
}
)";

	struct instance_t
	{
		float w[3][4];      // template space -> world
		float b[3][4];      // template space -> body space (studs, around the torso)
		float fill[4];
		float edge[4];
	};

	struct mesh_t
	{
		UINT start_index = 0;
		UINT index_count = 0;
		INT base_vertex = 0;
	};

	enum { mesh_box = 0, mesh_rounded, mesh_head, mesh_count };

	ID3D11Device* g_device = nullptr;
	ID3D11DeviceContext* g_context = nullptr;
	bool g_tried = false;
	bool g_ready = false;
	std::string g_error;

	ID3D11VertexShader* g_vs_body = nullptr;
	ID3D11PixelShader* g_ps_body = nullptr;
	ID3D11InputLayout* g_layout = nullptr;
	ID3D11VertexShader* g_vs_full = nullptr;
	ID3D11PixelShader* g_ps_comp = nullptr;
	ID3D11Buffer* g_vb = nullptr;
	ID3D11Buffer* g_ib = nullptr;
	ID3D11Buffer* g_instances = nullptr;
	UINT g_instance_capacity = 0;
	ID3D11Buffer* g_cb_frame = nullptr;
	ID3D11Buffer* g_cb_comp = nullptr;
	ID3D11DepthStencilState* g_depth = nullptr;
	ID3D11DepthStencilState* g_no_depth = nullptr;
	ID3D11RasterizerState* g_raster = nullptr;

	struct targets_t
	{
		UINT w = 0, h = 0;
		ID3D11Texture2D* tex[3]{};
		ID3D11RenderTargetView* rtv[3]{};
		ID3D11ShaderResourceView* srv[3]{};
		ID3D11Texture2D* depth = nullptr;
		ID3D11DepthStencilView* dsv = nullptr;
		ID3D11Texture2D* out = nullptr;
		ID3D11RenderTargetView* out_rtv = nullptr;
		ID3D11ShaderResourceView* out_srv = nullptr;
	};
	targets_t g_t;           // the world layer, under the ESP
	targets_t g_preview_t;   // the menu's Visuals preview

	mesh_t g_meshes[mesh_count];
	std::vector<instance_t> g_queue[mesh_count];
	bool g_begun = false;

	template <typename T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

	ID3DBlob* compile(const char* entry, const char* profile)
	{
		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		const HRESULT hr = D3DCompile(kShaders, sizeof(kShaders) - 1, "body_chams", nullptr, nullptr, entry, profile,
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
		if (FAILED(hr))
		{
			g_error = std::string("chams shader ") + entry + " didn't compile: "
				+ (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
			release(code);
		}
		release(errors);
		return code;
	}

	// ---- template shapes, all inside the unit cube [-0.5, 0.5] ----------------------------

	struct vec3f { float x, y, z; };

	void add_box(std::vector<vec3f>& v, std::vector<UINT>& idx)
	{
		const UINT base = static_cast<UINT>(v.size());
		for (int i = 0; i < 8; ++i)
			v.push_back({ (i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f });
		static const UINT faces[36] = {
			0, 2, 3, 0, 3, 1,   4, 5, 7, 4, 7, 6,   0, 1, 5, 0, 5, 4,
			2, 6, 7, 2, 7, 3,   0, 4, 6, 0, 6, 2,   1, 3, 7, 1, 7, 5 };
		for (UINT f : faces)
			idx.push_back(base + f);
	}

	// A box with its edges and corners rounded by `r` (in unit-cube terms): each face is a grid
	// whose points near the rim are pushed onto a quarter circle.
	void add_rounded_box(std::vector<vec3f>& v, std::vector<UINT>& idx, float r)
	{
		const float coords[] = { -0.5f, -0.5f + 0.3f * r, -0.5f + 0.7f * r, -0.5f + r, 0.5f - r, 0.5f - 0.7f * r, 0.5f - 0.3f * r, 0.5f };
		const int n = static_cast<int>(sizeof(coords) / sizeof(coords[0]));
		for (int axis = 0; axis < 3; ++axis)
		{
			for (int side = -1; side <= 1; side += 2)
			{
				const UINT base = static_cast<UINT>(v.size());
				const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
				for (int j = 0; j < n; ++j)
				{
					for (int i = 0; i < n; ++i)
					{
						float p[3];
						p[axis] = 0.5f * side;
						p[ua] = coords[i];
						p[va] = coords[j];
						float inner[3], d[3];
						float len = 0.0f;
						for (int k = 0; k < 3; ++k)
						{
							inner[k] = std::clamp(p[k], -0.5f + r, 0.5f - r);
							d[k] = p[k] - inner[k];
							len += d[k] * d[k];
						}
						len = std::sqrt(len);
						if (len > 1e-6f)
							for (int k = 0; k < 3; ++k)
								p[k] = inner[k] + d[k] / len * r;
						v.push_back({ p[0], p[1], p[2] });
					}
				}
				for (int j = 0; j + 1 < n; ++j)
				{
					for (int i = 0; i + 1 < n; ++i)
					{
						const UINT a = base + j * n + i, b = a + 1, c = a + n + 1, d = a + n;
						idx.insert(idx.end(), { a, b, c, a, c, d });
					}
				}
			}
		}
	}

	// A Roblox head: a cylinder around Y with rounded top and bottom edges.
	void add_head(std::vector<vec3f>& v, std::vector<UINT>& idx)
	{
		const float rf = 0.2f;
		std::vector<std::pair<float, float>> profile;   // (radius, y) from bottom centre to top centre
		profile.push_back({ 0.0f, -0.5f });
		for (int k = 0; k <= 4; ++k)
		{
			const float a = -1.5707963f + 1.5707963f * k / 4.0f;
			profile.push_back({ 0.5f - rf + rf * std::cos(a), -0.5f + rf + rf * std::sin(a) });
		}
		for (int k = 0; k <= 4; ++k)
		{
			const float a = 1.5707963f * k / 4.0f;
			profile.push_back({ 0.5f - rf + rf * std::cos(a), 0.5f - rf + rf * std::sin(a) });
		}
		profile.push_back({ 0.0f, 0.5f });

		const int segments = 24;
		const UINT base = static_cast<UINT>(v.size());
		for (const auto& [radius, y] : profile)
			for (int s = 0; s < segments; ++s)
			{
				const float phi = 6.2831853f * s / segments;
				v.push_back({ radius * std::cos(phi), y, radius * std::sin(phi) });
			}
		for (UINT ring = 0; ring + 1 < profile.size(); ++ring)
			for (int s = 0; s < segments; ++s)
			{
				const UINT a = base + ring * segments + s;
				const UINT b = base + ring * segments + (s + 1) % segments;
				const UINT c = b + segments, d = a + segments;
				idx.insert(idx.end(), { a, b, c, a, c, d });
			}
	}

	bool make_target(UINT w, UINT h, DXGI_FORMAT format, ID3D11Texture2D*& tex, ID3D11RenderTargetView*& rtv, ID3D11ShaderResourceView*& srv)
	{
		D3D11_TEXTURE2D_DESC td{};
		td.Width = w;
		td.Height = h;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = format;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		return SUCCEEDED(g_device->CreateTexture2D(&td, nullptr, &tex))
			&& SUCCEEDED(g_device->CreateRenderTargetView(tex, nullptr, &rtv))
			&& SUCCEEDED(g_device->CreateShaderResourceView(tex, nullptr, &srv));
	}

	void release_targets(targets_t& t)
	{
		for (int i = 0; i < 3; ++i)
		{
			release(t.srv[i]);
			release(t.rtv[i]);
			release(t.tex[i]);
		}
		release(t.dsv);
		release(t.depth);
		release(t.out_srv);
		release(t.out_rtv);
		release(t.out);
		t.w = t.h = 0;
	}

	bool ensure_targets(targets_t& t, UINT w, UINT h)
	{
		if (t.out && t.w == w && t.h == h)
			return true;
		release_targets(t);

		const DXGI_FORMAT formats[3] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM };
		bool ok = true;
		for (int i = 0; i < 3 && ok; ++i)
			ok = make_target(w, h, formats[i], t.tex[i], t.rtv[i], t.srv[i]);
		ok = ok && make_target(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, t.out, t.out_rtv, t.out_srv);

		if (ok)
		{
			D3D11_TEXTURE2D_DESC td{};
			td.Width = w;
			td.Height = h;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_D32_FLOAT;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			ok = SUCCEEDED(g_device->CreateTexture2D(&td, nullptr, &t.depth))
				&& SUCCEEDED(g_device->CreateDepthStencilView(t.depth, nullptr, &t.dsv));
		}
		if (!ok)
		{
			release_targets(t);
			g_error = "couldn't create the chams layer";
			return false;
		}
		t.w = w;
		t.h = h;
		return true;
	}

	bool init()
	{
		if (g_tried)
			return g_ready;
		g_tried = true;

		g_device = render->detail->device;
		g_context = render->detail->device_context;
		if (!g_device || !g_context)
		{
			g_error = "no graphics device";
			return false;
		}

		ID3DBlob* vs_body = compile("vs_body", "vs_4_0");
		ID3DBlob* ps_body = compile("ps_body", "ps_4_0");
		ID3DBlob* vs_full = compile("vs_full", "vs_4_0");
		ID3DBlob* ps_comp = compile("ps_comp", "ps_4_0");
		bool ok = vs_body && ps_body && vs_full && ps_comp;

		if (ok)
		{
			g_device->CreateVertexShader(vs_body->GetBufferPointer(), vs_body->GetBufferSize(), nullptr, &g_vs_body);
			g_device->CreatePixelShader(ps_body->GetBufferPointer(), ps_body->GetBufferSize(), nullptr, &g_ps_body);
			g_device->CreateVertexShader(vs_full->GetBufferPointer(), vs_full->GetBufferSize(), nullptr, &g_vs_full);
			g_device->CreatePixelShader(ps_comp->GetBufferPointer(), ps_comp->GetBufferSize(), nullptr, &g_ps_comp);

			const D3D11_INPUT_ELEMENT_DESC layout[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,   D3D11_INPUT_PER_VERTEX_DATA,   0 },
				{ "WORLD",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,   D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "WORLD",    1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "WORLD",    2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "BODY",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "BODY",     1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "BODY",     2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "FILL",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 96,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "EDGE",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			};
			g_device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)), vs_body->GetBufferPointer(), vs_body->GetBufferSize(), &g_layout);
			ok = g_vs_body && g_ps_body && g_vs_full && g_ps_comp && g_layout;
			if (!ok)
				g_error = "couldn't create the chams shaders";
		}
		release(vs_body);
		release(ps_body);
		release(vs_full);
		release(ps_comp);

		if (ok)
		{
			std::vector<vec3f> verts;
			std::vector<UINT> idx;
			auto record = [&](int which, auto&& build)
			{
				g_meshes[which].base_vertex = static_cast<INT>(verts.size());
				g_meshes[which].start_index = static_cast<UINT>(idx.size());
				std::vector<vec3f> v;
				std::vector<UINT> i;
				build(v, i);
				verts.insert(verts.end(), v.begin(), v.end());
				idx.insert(idx.end(), i.begin(), i.end());
				g_meshes[which].index_count = static_cast<UINT>(i.size());
			};
			record(mesh_box, [](auto& v, auto& i) { add_box(v, i); });
			record(mesh_rounded, [](auto& v, auto& i) { add_rounded_box(v, i, 0.2f); });
			record(mesh_head, [](auto& v, auto& i) { add_head(v, i); });

			D3D11_BUFFER_DESC bd{};
			bd.Usage = D3D11_USAGE_IMMUTABLE;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.ByteWidth = static_cast<UINT>(verts.size() * sizeof(vec3f));
			D3D11_SUBRESOURCE_DATA sd{ verts.data() };
			ok = SUCCEEDED(g_device->CreateBuffer(&bd, &sd, &g_vb));
			bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
			bd.ByteWidth = static_cast<UINT>(idx.size() * sizeof(UINT));
			sd.pSysMem = idx.data();
			ok = ok && SUCCEEDED(g_device->CreateBuffer(&bd, &sd, &g_ib));

			D3D11_BUFFER_DESC cb{};
			cb.Usage = D3D11_USAGE_DYNAMIC;
			cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			cb.ByteWidth = 64;
			ok = ok && SUCCEEDED(g_device->CreateBuffer(&cb, nullptr, &g_cb_frame));
			cb.ByteWidth = 16;
			ok = ok && SUCCEEDED(g_device->CreateBuffer(&cb, nullptr, &g_cb_comp));

			D3D11_DEPTH_STENCIL_DESC dd{};
			dd.DepthEnable = TRUE;
			dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			dd.DepthFunc = D3D11_COMPARISON_GREATER;   // reversed depth: nearer is bigger
			ok = ok && SUCCEEDED(g_device->CreateDepthStencilState(&dd, &g_depth));
			dd.DepthEnable = FALSE;
			dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			ok = ok && SUCCEEDED(g_device->CreateDepthStencilState(&dd, &g_no_depth));

			D3D11_RASTERIZER_DESC rd{};
			rd.FillMode = D3D11_FILL_SOLID;
			rd.CullMode = D3D11_CULL_NONE;
			rd.DepthClipEnable = TRUE;
			ok = ok && SUCCEEDED(g_device->CreateRasterizerState(&rd, &g_raster));
			if (!ok)
				g_error = "couldn't create the chams buffers";
		}

		g_ready = ok;
		return ok;
	}

	template <typename T>
	void upload(ID3D11Buffer* buffer, const T* data, size_t bytes)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(g_context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			std::memcpy(mapped.pData, data, bytes);
			g_context->Unmap(buffer, 0);
		}
	}

	bool ensure_instance_capacity(UINT count)
	{
		if (count <= g_instance_capacity && g_instances)
			return true;
		release(g_instances);
		g_instance_capacity = (std::max)(count, 256u);
		D3D11_BUFFER_DESC bd{};
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.ByteWidth = g_instance_capacity * sizeof(instance_t);
		if (FAILED(g_device->CreateBuffer(&bd, nullptr, &g_instances)))
		{
			g_instance_capacity = 0;
			return false;
		}
		return true;
	}
}

namespace
{
	void queue_body(std::vector<instance_t>* queue, const rig::body_t& body, const float fill[4], const float outline[4])
	{
		if (body.parts.empty())
			return;

		// Body space is centred on the torso and turns with it.
		const rig::part_t* root = body.find("UpperTorso");
		if (!root)
			root = body.find("Torso");
		if (!root)
			root = &body.parts.front();
		const math::matrix3& rr = root->rot;

		for (const rig::part_t& p : body.parts)
		{
			int mesh = body.kind == rig::kind_t::r6 ? mesh_box : mesh_rounded;
			// A little bigger than the part, so neighbours overlap and no seam between limbs
			// shows up as an outline inside the body.
			math::vector3 scale{ p.size.x + 0.08f, p.size.y + 0.08f, p.size.z + 0.08f };
			if (p.name == "Head")
			{
				mesh = mesh_head;
				if (body.kind == rig::kind_t::r6)
				{
					// An R6 head part is 2x1x1, but the head you see is a 1.25-stud rounded cylinder.
					const float s = p.size.y * 1.25f;
					scale = { s, s, s };
				}
			}

			instance_t inst{};
			float m[3][3];
			for (int i = 0; i < 3; ++i)
			{
				for (int j = 0; j < 3; ++j)
				{
					m[i][j] = p.rot.m[i][j] * scale[j];
					inst.w[i][j] = m[i][j];
				}
				inst.w[i][3] = p.pos[i];
			}

			const math::vector3 rel = p.pos - root->pos;
			for (int i = 0; i < 3; ++i)
			{
				// row i of rootᵀ is column i of the root's rotation
				for (int j = 0; j < 3; ++j)
					inst.b[i][j] = rr.m[0][i] * m[0][j] + rr.m[1][i] * m[1][j] + rr.m[2][i] * m[2][j];
				inst.b[i][3] = rr.m[0][i] * rel.x + rr.m[1][i] * rel.y + rr.m[2][i] * rel.z;
			}
			std::memcpy(inst.fill, fill, sizeof(inst.fill));
			std::memcpy(inst.edge, outline, sizeof(inst.edge));
			queue[mesh].push_back(inst);
		}
	}

	// Renders one layer: bodies into the three targets (viewport = where Roblox's render area
	// sits, exactly what world_to_screen does), then fill, effect, outline and glow into `out`.
	void draw_layer(targets_t& t, const std::vector<instance_t>* queue, const body_chams::frame_t& frame)
	{
		const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		UINT total = 0;
		for (int i = 0; i < mesh_count; ++i)
			total += static_cast<UINT>(queue[i].size());
		if (total == 0 || !ensure_instance_capacity(total))
		{
			g_context->ClearRenderTargetView(t.out_rtv, clear);
			return;
		}

		std::vector<instance_t> all;
		all.reserve(total);
		UINT first[mesh_count]{};
		for (int i = 0; i < mesh_count; ++i)
		{
			first[i] = static_cast<UINT>(all.size());
			all.insert(all.end(), queue[i].begin(), queue[i].end());
		}
		upload(g_instances, all.data(), all.size() * sizeof(instance_t));
		upload(g_cb_frame, &frame.view_projection, sizeof(math::matrix4));

		for (int i = 0; i < 3; ++i)
			g_context->ClearRenderTargetView(t.rtv[i], clear);
		g_context->ClearDepthStencilView(t.dsv, D3D11_CLEAR_DEPTH, 0.0f, 0);

		D3D11_VIEWPORT vp{};
		vp.TopLeftX = frame.offset.x;
		vp.TopLeftY = frame.offset.y;
		vp.Width = frame.dims.x;
		vp.Height = frame.dims.y;
		vp.MaxDepth = 1.0f;
		g_context->RSSetViewports(1, &vp);
		g_context->RSSetState(g_raster);
		g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		g_context->OMSetDepthStencilState(g_depth, 0);
		g_context->OMSetRenderTargets(3, t.rtv, t.dsv);

		ID3D11Buffer* buffers[2] = { g_vb, g_instances };
		const UINT strides[2] = { sizeof(vec3f), sizeof(instance_t) };
		const UINT offsets[2] = { 0, 0 };
		g_context->IASetVertexBuffers(0, 2, buffers, strides, offsets);
		g_context->IASetIndexBuffer(g_ib, DXGI_FORMAT_R32_UINT, 0);
		g_context->IASetInputLayout(g_layout);
		g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_context->VSSetShader(g_vs_body, nullptr, 0);
		g_context->VSSetConstantBuffers(0, 1, &g_cb_frame);
		g_context->GSSetShader(nullptr, nullptr, 0);
		g_context->PSSetShader(g_ps_body, nullptr, 0);
		for (int i = 0; i < mesh_count; ++i)
			if (!queue[i].empty())
				g_context->DrawIndexedInstanced(g_meshes[i].index_count, static_cast<UINT>(queue[i].size()),
					g_meshes[i].start_index, g_meshes[i].base_vertex, first[i]);

		struct comp_t
		{
			float time, scale, edge;
			int effect;
		} comp{
			static_cast<float>(std::fmod(ImGui::GetTime() * frame.speed, 10000.0)),
			frame.scale, frame.outline_width, frame.effect };
		upload(g_cb_comp, &comp, sizeof(comp));

		ID3D11RenderTargetView* none_rtv[3] = { nullptr, nullptr, nullptr };
		g_context->OMSetRenderTargets(3, none_rtv, nullptr);
		D3D11_VIEWPORT full{};
		full.Width = static_cast<float>(t.w);
		full.Height = static_cast<float>(t.h);
		full.MaxDepth = 1.0f;
		g_context->RSSetViewports(1, &full);
		g_context->OMSetDepthStencilState(g_no_depth, 0);
		g_context->OMSetRenderTargets(1, &t.out_rtv, nullptr);
		g_context->IASetInputLayout(nullptr);
		g_context->VSSetShader(g_vs_full, nullptr, 0);
		g_context->PSSetShader(g_ps_comp, nullptr, 0);
		g_context->PSSetConstantBuffers(0, 1, &g_cb_comp);
		g_context->PSSetShaderResources(0, 3, t.srv);
		g_context->Draw(3, 0);

		// Leave nothing bound that ImGui's pass would trip over (it sets everything else itself).
		ID3D11ShaderResourceView* none_srv[3] = { nullptr, nullptr, nullptr };
		g_context->PSSetShaderResources(0, 3, none_srv);
		g_context->OMSetRenderTargets(1, none_rtv, nullptr);
	}
}

bool body_chams::begin(ImDrawList* draw)
{
	g_begun = false;
	for (auto& q : g_queue)
		q.clear();
	if (!init())
		return false;

	const ImVec2 size = ImGui::GetIO().DisplaySize;
	const UINT w = static_cast<UINT>((std::max)(1.0f, size.x));
	const UINT h = static_cast<UINT>((std::max)(1.0f, size.y));
	if (!ensure_targets(g_t, w, h))
		return false;

	draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(g_t.out_srv)), ImVec2(0, 0), ImVec2(static_cast<float>(w), static_cast<float>(h)));
	g_begun = true;
	return true;
}

void body_chams::add(const rig::body_t& body, const float fill[4], const float outline[4])
{
	if (g_begun)
		queue_body(g_queue, body, fill, outline);
}

void body_chams::end(const frame_t& frame)
{
	if (!g_begun)
		return;
	g_begun = false;
	draw_layer(g_t, g_queue, frame);
}

ImTextureID body_chams::render_preview(const rig::body_t* bodies, int count, const float fill[4], const float outline[4], const frame_t& frame)
{
	if (!init())
		return 0;
	const UINT w = static_cast<UINT>((std::max)(1.0f, frame.dims.x));
	const UINT h = static_cast<UINT>((std::max)(1.0f, frame.dims.y));
	if (!ensure_targets(g_preview_t, w, h))
		return 0;

	std::vector<instance_t> queue[mesh_count];
	for (int i = 0; i < count; ++i)
		queue_body(queue, bodies[i], fill, outline);
	frame_t local = frame;
	local.offset = { 0.0f, 0.0f };
	draw_layer(g_preview_t, queue, local);
	return static_cast<ImTextureID>(reinterpret_cast<intptr_t>(g_preview_t.out_srv));
}

const std::string& body_chams::error()
{
	return g_error;
}
