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
    // DarkVeil.jsx defaults.
    constexpr float    kSpeed      = 0.5f;
    constexpr float    kFlow       = 1.0f;
    constexpr float    kGlow       = 0.4f;
    constexpr float    kGrain      = 0.03f;
    constexpr float    kQuality    = 0.5f;     // field resolution relative to the panel
    constexpr float    kIntro      = 1.8f;     // seconds
    constexpr float    kMouse      = 1.0f;     // mouseStrength
    constexpr unsigned kColor      = 0x6B12FF;
    constexpr unsigned kBackground = 0x0A0A0A; // what the transparent canvas sits on

    // HLSL port of the FIELD and COMPOSITE shaders. GLSL `mat4(cols) * v` becomes
    // `mul(v, float4x4(rows))`, which is the same sum. D3D's SV_Position is y-down, which is
    // already the space the GLSL reaches after its `uv.y = -uv.y`, so that flip is dropped.
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

float4 sigmoid(float4 x) { return 1.0 / (1.0 + exp(-x)); }
)";

    // Filled in from DarkVeil.jsx's cppn() by the conversion described above.
    const char kCppn[] = R"(
float4 cppn(float2 coordinate, float3 latent) {
  float4 buf[8];
  buf[6]=float4(coordinate.x,coordinate.y,latent.x,latent.y);
  buf[7]=float4(latent.z,sqrt(coordinate.x*coordinate.x+coordinate.y*coordinate.y),0.,0.);
  buf[0]=mul(buf[6],float4x4(float4(6.5404263,-3.6126034,0.7590882,-1.13613),float4(2.4582713,3.1660357,1.2219609,0.06276096),float4(-5.478085,-6.159632,1.8701609,-4.7742867),float4(6.039214,-5.542865,-0.90925294,3.251348)))+mul(buf[7],float4x4(float4(0.8473259,-5.722911,3.975766,1.6522468),float4(-0.24321538,0.5839259,-1.7661959,-5.350116),float4(0.,0.,0.,0.),float4(0.,0.,0.,0.)))+float4(0.21808943,1.1243913,-1.7969975,5.0294676);
  buf[1]=mul(buf[6],float4x4(float4(-3.3522482,-6.0612736,0.55641043,-4.4719114),float4(0.8631464,1.7432913,5.643898,1.6106541),float4(2.4941394,-3.5012043,1.7184316,6.357333),float4(3.310376,8.209261,1.1355612,-1.165539)))+mul(buf[7],float4x4(float4(5.24046,-13.034365,0.009859298,15.870829),float4(2.987511,3.129433,-0.89023495,-1.6822904),float4(0.,0.,0.,0.),float4(0.,0.,0.,0.)))+float4(-5.9457836,-6.573602,-0.8812491,1.5436668);
  buf[0]=sigmoid(buf[0]);buf[1]=sigmoid(buf[1]);
  buf[2]=mul(buf[6],float4x4(float4(-15.219568,8.095543,-2.429353,-1.9381982),float4(-5.951362,4.3115187,2.6393783,1.274315),float4(-7.3145227,6.7297835,5.2473326,5.9411426),float4(5.0796127,8.979051,-1.7278991,-1.158976)))+mul(buf[7],float4x4(float4(-11.967154,-11.608155,6.1486754,11.237008),float4(2.124141,-6.263192,-1.7050359,-0.7021966),float4(0.,0.,0.,0.),float4(0.,0.,0.,0.)))+float4(-4.17164,-3.2281182,-4.576417,-3.6401186);
  buf[3]=mul(buf[6],float4x4(float4(3.1832156,-13.738922,1.879223,3.233465),float4(0.64300746,12.768129,1.9141049,0.50990224),float4(-0.049295485,4.4807224,1.4733979,1.801449),float4(5.0039253,13.000481,3.3991797,-4.5561905)))+mul(buf[7],float4x4(float4(-0.1285731,7.720628,-3.1425676,4.742367),float4(0.6393625,3.714393,-0.8108378,-0.39174938),float4(0.,0.,0.,0.),float4(0.,0.,0.,0.)))+float4(-1.1811101,-21.621881,0.7851888,1.2329718);
  buf[2]=sigmoid(buf[2]);buf[3]=sigmoid(buf[3]);
  buf[4]=mul(buf[0],float4x4(float4(5.214916,-7.183024,2.7228765,2.6592617),float4(-5.601878,-25.3591,4.067988,0.4602802),float4(-10.57759,24.286327,21.102104,37.546658),float4(4.3024497,-1.9625226,2.3458803,-1.372816)))+mul(buf[1],float4x4(float4(-17.6526,-10.507558,2.2587414,12.462782),float4(6.265566,-502.75443,-12.642513,0.9112289),float4(-10.983244,20.741234,-9.701768,-0.7635988),float4(5.383626,1.4819539,-4.1911616,-4.8444734)))+mul(buf[2],float4x4(float4(12.785233,-16.345072,-0.39901125,1.7955981),float4(-30.48365,-1.8345358,1.4542528,-1.1118771),float4(19.872723,-7.337935,-42.941723,-98.52709),float4(8.337645,-2.7312303,-2.2927687,-36.142323)))+mul(buf[3],float4x4(float4(-16.298317,3.5471997,-0.44300047,-9.444417),float4(57.5077,-35.609753,16.163465,-4.1534753),float4(-0.07470326,-3.8656476,-7.0901804,3.1523974),float4(-12.559385,-7.077619,1.490437,-0.8211543)))+float4(-7.67914,15.927437,1.3207729,-1.6686112);
  buf[5]=mul(buf[0],float4x4(float4(-1.4109162,-0.372762,-3.770383,-21.367174),float4(-6.2103205,-9.35908,0.92529047,8.82561),float4(11.460242,-22.348068,13.625772,-18.693201),float4(-0.3429052,-3.9905605,-2.4626114,-0.45033523)))+mul(buf[1],float4x4(float4(7.3481627,-4.3661838,-6.3037653,-3.868115),float4(1.5462853,6.5488915,1.9701879,-0.58291394),float4(6.5858274,-2.2180402,3.7127688,-1.3730392),float4(-5.7973905,10.134961,-2.3395722,-5.965605)))+mul(buf[2],float4x4(float4(-2.5132585,-6.6685553,-1.4029363,-0.16285264),float4(-0.37908727,0.53738135,4.389061,-1.3024765),float4(-0.70647055,2.0111287,-5.1659346,-3.728635),float4(-13.562562,10.487719,-0.9173751,-2.6487076)))+mul(buf[3],float4x4(float4(-8.645013,6.5546675,-6.3944063,-5.5933375),float4(-0.57783127,-1.077275,36.91025,5.736769),float4(14.283112,3.7146652,7.1452246,-4.5958776),float4(2.7192075,3.6021907,-4.366337,-2.3653464)))+float4(-5.9000807,-4.329569,1.2427121,8.59503);
  buf[4]=sigmoid(buf[4]);buf[5]=sigmoid(buf[5]);
  buf[6]=mul(buf[0],float4x4(float4(-1.61102,0.7970257,1.4675229,0.20917463),float4(-28.793737,-7.1390953,1.5025433,4.656581),float4(-10.94861,39.66238,0.74318546,-10.095605),float4(-0.7229728,-1.5483948,0.7301322,2.1687684)))+mul(buf[1],float4x4(float4(3.2547753,21.489103,-1.0194173,-3.3100595),float4(-3.7316632,-3.3792162,-7.223193,-0.23685838),float4(13.1804495,0.7916005,5.338587,5.687114),float4(-4.167605,-17.798311,-6.815736,-1.6451967)))+mul(buf[2],float4x4(float4(0.604885,-7.800309,-7.213122,-2.741014),float4(-3.522382,-0.12359311,-0.5258442,0.43852118),float4(9.6752825,-22.853785,2.062431,0.099892326),float4(-4.3196306,-17.730087,2.5184598,5.30267)))+mul(buf[3],float4x4(float4(-6.545563,-15.790176,-6.0438633,-5.415399),float4(-43.591583,28.551912,-16.00161,18.84728),float4(4.212382,8.394307,3.0958717,8.657522),float4(-5.0237565,-4.450633,-4.4768,-5.5010443)))+mul(buf[4],float4x4(float4(1.6985557,-67.05806,6.897715,1.9004834),float4(1.8680354,2.3915145,2.5231109,4.081538),float4(11.158006,1.7294737,2.0738268,7.386411),float4(-4.256034,-306.24686,8.258898,-17.132736)))+mul(buf[5],float4x4(float4(1.6889864,-4.5852966,3.8534803,-6.3482175),float4(1.3543309,-1.2640043,9.932754,2.9079645),float4(-5.2770967,0.07150358,-0.13962056,3.3269649),float4(28.34703,-4.918278,6.1044083,4.085355)))+float4(6.6818056,12.522166,-3.7075126,-4.104386);
  buf[7]=mul(buf[0],float4x4(float4(-8.265602,-4.7027016,5.098234,0.7509808),float4(8.6507845,-17.15949,16.51939,-8.884479),float4(-4.036479,-2.3946867,-2.6055532,-1.9866527),float4(-2.2167742,-1.8135649,-5.9759874,4.8846445)))+mul(buf[1],float4x4(float4(6.7790847,3.5076547,-2.8191125,-2.7028968),float4(-5.743024,-0.27844876,1.4958696,-5.0517144),float4(13.122226,15.735168,-2.9397483,-4.101023),float4(-14.375265,-5.030483,-6.2599335,2.9848232)))+mul(buf[2],float4x4(float4(4.0950394,-0.94011575,-5.674733,4.755022),float4(4.3809423,4.8310084,1.7425908,-3.437416),float4(2.117492,0.16342592,-104.56341,16.949184),float4(-5.22543,-2.994248,3.8350096,-1.9364246)))+mul(buf[3],float4x4(float4(-5.900337,1.7946124,-13.604192,-3.8060522),float4(6.6583457,31.911177,25.164474,91.81147),float4(11.840538,4.1503043,-0.7314397,6.768467),float4(-6.3967767,4.034772,6.1714606,-0.32874924)))+mul(buf[4],float4x4(float4(3.4992442,-196.91893,-8.923708,2.8142626),float4(3.4806502,-3.1846354,5.1725626,5.1804223),float4(-2.4009497,15.585794,1.2863957,2.0252278),float4(-71.25271,-62.441242,-8.138444,0.50670296)))+mul(buf[5],float4x4(float4(-12.291733,-11.176166,-7.3474145,4.390294),float4(10.805477,5.6337385,-0.9385842,-4.7348723),float4(-12.869276,-7.039391,5.3029537,7.5436664),float4(1.4593618,8.91898,3.5101583,5.840625)))+float4(2.2415268,-6.705987,-0.98861027,-2.117676);
  buf[6]=sigmoid(buf[6]);buf[7]=sigmoid(buf[7]);
  buf[0]=mul(buf[0],float4x4(float4(1.6794263,1.3817469,2.9625452,0.),float4(-1.8834411,-1.4806935,-3.5924516,0.),float4(-1.3279216,-1.0918057,-2.3124623,0.),float4(0.2662234,0.23235129,0.44178495,0.)))+mul(buf[1],float4x4(float4(-0.6299101,-0.5945583,-0.9125601,0.),float4(0.17828953,0.18300213,0.18182953,0.),float4(-2.96544,-2.5819945,-4.9001055,0.),float4(1.4195864,1.1868085,2.5176322,0.)))+mul(buf[2],float4x4(float4(-1.2584374,-1.0552157,-2.1688404,0.),float4(-0.7200217,-0.52666044,-1.438251,0.),float4(0.15345335,0.15196142,0.272854,0.),float4(0.945728,0.8861938,1.2766753,0.)))+mul(buf[3],float4x4(float4(-2.4218085,-1.968602,-4.35166,0.),float4(-22.683098,-18.0544,-41.954372,0.),float4(0.63792,0.5470648,1.1078634,0.),float4(-1.5489894,-1.3075932,-2.6444845,0.)))+mul(buf[4],float4x4(float4(-0.49252132,-0.39877754,-0.91366625,0.),float4(0.95609266,0.7923952,1.640221,0.),float4(0.30616966,0.15693925,0.8639857,0.),float4(1.1825981,0.94504964,2.176963,0.)))+mul(buf[5],float4x4(float4(0.35446745,0.3293795,0.59547555,0.),float4(-0.58784515,-0.48177817,-1.0614829,0.),float4(2.5271258,1.9991658,4.6846647,0.),float4(0.13042648,0.08864098,0.30187556,0.)))+mul(buf[6],float4x4(float4(-1.7718065,-1.4033192,-3.3355875,0.),float4(3.1664357,2.638297,5.378702,0.),float4(-3.1724713,-2.6107926,-5.549295,0.),float4(-2.851368,-2.249092,-5.3013067,0.)))+mul(buf[7],float4x4(float4(1.5203838,1.2212278,2.8404984,0.),float4(1.5210563,1.2651345,2.683903,0.),float4(2.9789467,2.4364579,5.2347264,0.),float4(2.2270417,1.8825914,3.8028636,0.)))+float4(-1.5468478,-3.6171484,0.24762098,0.);
  buf[0]=sigmoid(buf[0]);
  return float4(buf[0].x,buf[0].y,buf[0].z,1.);
}
)";

    const char kField[] = R"(
float hash(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float2 rotate(float a, float2 v)
{
    float c = cos(a), s = sin(a);
    return float2(c * v.x - s * v.y, s * v.x + c * v.y);
}

float4 ps_field(float4 pos : SV_Position) : SV_Target
{
    float2 css = pos.xy * uScale + uOffset;
    float2 uv = (css * 2.0 - uView) / uView.y;
    float2 anchor = float2(uView.x / uView.y, -1.0);

    float2 pointer = float2(uPointer.x * 2.0 - uView.x, uPointer.y * 2.0 - uView.y) / uView.y;
    float2 offset = uv - pointer;
    float touch = uPointer.z * exp(-dot(offset, offset) / (uPointer.w * uPointer.w));
    uv = pointer + rotate(touch * 0.6, offset);
    uv -= uDrag * touch;

    float t = uTime;
    uv = rotate(0.06 * uFlow * sin(t * 0.17), uv);
    uv += 0.05 * uFlow * float2(sin(t * 0.21), cos(t * 0.13));
    uv = anchor + float2(uTurn.x * uv.x - uTurn.y * uv.y, uTurn.y * uv.x + uTurn.x * uv.y) * 2.0 / uZoom;
    uv += uWarp * float2(sin(uv.y * 6.283 + t * 0.5), cos(uv.x * 6.283 + t * 0.5)) * 0.05;

    float3 latent = uSeed + 0.1 * uFlow * float3(sin(0.3 * t), sin(0.69 * t), sin(0.44 * t));
    latent += touch * float3(0.12, -0.1, 0.16);
    latent.z += uUnfold;

    float3 veil = cppn(uv, latent).rgb;
    veil += (hash(pos.xy) - 0.5) * uDither;
    return float4(veil, 1.0);
}
)";

    const char kComposite[] = R"(
Texture2D uField : register(t0);
SamplerState uSampler : register(s0);

cbuffer CompositeParams : register(b0)
{
    float2 uResolution; float2 uTexel;
    float3 uColor; float uGain;
    float uSheen; float uGlowAmount; float uPresence; float uGrain;
    float uClock; float uPad0; float uPad1; float uPad2;
    float4 uLight;
    float3 uSky; float uPad3;
};

float hash2(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float level(float2 uv, float lod) { return uField.SampleLevel(uSampler, uv, lod).b; }

float4 ps_composite(float4 pos : SV_Position) : SV_Target
{
    float2 uv = pos.xy / uResolution;
    float base = saturate(level(uv, 0.0));
    float glow = level(uv, 2.0) * 0.4 + level(uv, 3.0) * 0.3 + level(uv, 4.0) * 0.2 + level(uv, 5.0) * 0.1;

    // Same lighting as the original; every vector is in the y-down frame, so the dot
    // products match the y-up GLSL exactly.
    float dx = level(uv + float2(uTexel.x, 0.0), 0.0) - level(uv - float2(uTexel.x, 0.0), 0.0);
    float dy = level(uv + float2(0.0, uTexel.y), 0.0) - level(uv - float2(0.0, uTexel.y), 0.0);
    float2 slope = float2(dx / (2.0 * uTexel.x) * (uResolution.y / uResolution.x), dy / (2.0 * uTexel.y));
    float3 normal = normalize(float3(-slope * 0.12, 1.0));
    float2 pixel = pos.xy / uResolution.y;
    float3 toLight = normalize(float3(uLight.xy - pixel, 0.7));
    float3 halfway = normalize(toLight + float3(0.0, 0.0, 1.0));
    float facing = max(dot(normal, halfway), 0.0);
    float sheen = pow(facing, 40.0) * smoothstep(0.08, 0.7, base);
    float near = uLight.z * exp(-dot(uLight.xy - pixel, uLight.xy - pixel) / 0.2);

    float energy = pow(base, 2.2) * uGain * (1.0 + near * 0.4);
    energy += pow(glow, 2.2) * uGlowAmount * uGain;
    energy *= uPresence;
    float3 light = uColor * energy;
    light += uColor * energy * energy * 0.35;
    float3 gleam = lerp(uColor * 2.0, float3(1.0, 1.0, 1.0), 0.55);
    light += gleam * sheen * uSheen * (0.8 + near * 0.4) * uPresence;

    float grain = (hash2(pos.xy + frac(uClock * 7.31) * 113.0) - 0.5) * uGrain;
    float dither = (hash2(pos.yx + 31.7) - 0.5) / 255.0;
    float peak = max(light.r, max(light.g, light.b));

    float3 color = 1.0 - exp(-light);
    color = pow(color, 1.0 / 2.2);
    color = saturate(color + grain * peak + dither);
    // The canvas is transparent with premultiplied alpha = max(rgb); composite it onto the page colour.
    float alpha = max(color.r, max(color.g, color.b));
    return float4(color + uSky * (1.0 - alpha), 1.0);
}
)";

    // GlassSurface.jsx in its refractive (Chromium SVG) mode. The component renders an SVG
    // displacement map and runs the backdrop through feDisplacementMap once per colour channel;
    // here the map is evaluated analytically. Checked against Chrome's own raster of the map:
    //  - black, then red = (1-u)^2 across and blue = v^2 down (SVG gradients from transparent
    //    interpolate unpremultiplied, hence the squares; blue is unused by the default x=R, y=G);
    //  - a 50% grey rect inset by min(w,h) * borderWidth / 2 at 93% opacity, blurred by `blur`,
    //    i.e. map = lerp(base, 0.5, 0.93 * gaussian_box).
    // Then the dark-scheme box-shadows: three faint inset ink shadows. Its two inset white rims
    // are dropped: the user disliked the white outline they draw.
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
        float color[3], gain;
        float sheen, glow, presence, grain;
        float clock, pad0, pad1, pad2;
        float light[4];
        float sky[3], pad3;
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
    src += kCppn;
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
    fp.zoom = 1.0f;
    fp.time = s.clock;
    fp.flow = kFlow;
    fp.warp = 0.0f;
    fp.turn[0] = 1.0f;
    fp.turn[1] = 0.0f;
    fp.unfold = (1.0f - presence) * 0.45f;
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
    Linear(cp.color, kColor);
    cp.gain = 1.15f;
    cp.sheen = 0.0f;
    cp.glow = kGlow;
    cp.presence = presence;
    cp.grain = kGrain;
    cp.clock = fmodf(s.clock, 1000.0f);
    cp.light[0] = s.light_x * size.x / size.y;
    cp.light[1] = s.light_y;   // y-down, so no 1 - y
    cp.light[2] = s.touch * kMouse;
    cp.sky[0] = ((kBackground >> 16) & 0xFF) / 255.0f;
    cp.sky[1] = ((kBackground >> 8) & 0xFF) / 255.0f;
    cp.sky[2] = (kBackground & 0xFF) / 255.0f;
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
        dl->AddRectFilled(min, max, IM_COL32(16, 16, 20, 205), radius);
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
