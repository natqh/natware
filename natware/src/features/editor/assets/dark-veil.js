// React Bits "Dark Veil", the same port the external menu renders (src/menu/ui/veil.cpp):
// the DarkVeil.jsx CPPN field at half resolution into a mipmapped texture, then the menu's
// composite pass (glow from the mip chain, tone map, grain) in #6B12FF over #0A0A0A.
// Pointer interaction and the intro fade follow veil.cpp's RenderInstance.
(function () {
  const CPPN_SOURCE = "vec4 cppn(vec2 coordinate, vec3 latent) {\n  vec4 buf[8];\n  buf[6]=vec4(coordinate.x,coordinate.y,latent.x,latent.y);\n  buf[7]=vec4(latent.z,sqrt(coordinate.x*coordinate.x+coordinate.y*coordinate.y),0.,0.);\n  buf[0]=mat4(vec4(6.5404263,-3.6126034,0.7590882,-1.13613),vec4(2.4582713,3.1660357,1.2219609,0.06276096),vec4(-5.478085,-6.159632,1.8701609,-4.7742867),vec4(6.039214,-5.542865,-0.90925294,3.251348))*buf[6]+mat4(vec4(0.8473259,-5.722911,3.975766,1.6522468),vec4(-0.24321538,0.5839259,-1.7661959,-5.350116),vec4(0.,0.,0.,0.),vec4(0.,0.,0.,0.))*buf[7]+vec4(0.21808943,1.1243913,-1.7969975,5.0294676);\n  buf[1]=mat4(vec4(-3.3522482,-6.0612736,0.55641043,-4.4719114),vec4(0.8631464,1.7432913,5.643898,1.6106541),vec4(2.4941394,-3.5012043,1.7184316,6.357333),vec4(3.310376,8.209261,1.1355612,-1.165539))*buf[6]+mat4(vec4(5.24046,-13.034365,0.009859298,15.870829),vec4(2.987511,3.129433,-0.89023495,-1.6822904),vec4(0.,0.,0.,0.),vec4(0.,0.,0.,0.))*buf[7]+vec4(-5.9457836,-6.573602,-0.8812491,1.5436668);\n  buf[0]=sigmoid(buf[0]);buf[1]=sigmoid(buf[1]);\n  buf[2]=mat4(vec4(-15.219568,8.095543,-2.429353,-1.9381982),vec4(-5.951362,4.3115187,2.6393783,1.274315),vec4(-7.3145227,6.7297835,5.2473326,5.9411426),vec4(5.0796127,8.979051,-1.7278991,-1.158976))*buf[6]+mat4(vec4(-11.967154,-11.608155,6.1486754,11.237008),vec4(2.124141,-6.263192,-1.7050359,-0.7021966),vec4(0.,0.,0.,0.),vec4(0.,0.,0.,0.))*buf[7]+vec4(-4.17164,-3.2281182,-4.576417,-3.6401186);\n  buf[3]=mat4(vec4(3.1832156,-13.738922,1.879223,3.233465),vec4(0.64300746,12.768129,1.9141049,0.50990224),vec4(-0.049295485,4.4807224,1.4733979,1.801449),vec4(5.0039253,13.000481,3.3991797,-4.5561905))*buf[6]+mat4(vec4(-0.1285731,7.720628,-3.1425676,4.742367),vec4(0.6393625,3.714393,-0.8108378,-0.39174938),vec4(0.,0.,0.,0.),vec4(0.,0.,0.,0.))*buf[7]+vec4(-1.1811101,-21.621881,0.7851888,1.2329718);\n  buf[2]=sigmoid(buf[2]);buf[3]=sigmoid(buf[3]);\n  buf[4]=mat4(vec4(5.214916,-7.183024,2.7228765,2.6592617),vec4(-5.601878,-25.3591,4.067988,0.4602802),vec4(-10.57759,24.286327,21.102104,37.546658),vec4(4.3024497,-1.9625226,2.3458803,-1.372816))*buf[0]+mat4(vec4(-17.6526,-10.507558,2.2587414,12.462782),vec4(6.265566,-502.75443,-12.642513,0.9112289),vec4(-10.983244,20.741234,-9.701768,-0.7635988),vec4(5.383626,1.4819539,-4.1911616,-4.8444734))*buf[1]+mat4(vec4(12.785233,-16.345072,-0.39901125,1.7955981),vec4(-30.48365,-1.8345358,1.4542528,-1.1118771),vec4(19.872723,-7.337935,-42.941723,-98.52709),vec4(8.337645,-2.7312303,-2.2927687,-36.142323))*buf[2]+mat4(vec4(-16.298317,3.5471997,-0.44300047,-9.444417),vec4(57.5077,-35.609753,16.163465,-4.1534753),vec4(-0.07470326,-3.8656476,-7.0901804,3.1523974),vec4(-12.559385,-7.077619,1.490437,-0.8211543))*buf[3]+vec4(-7.67914,15.927437,1.3207729,-1.6686112);\n  buf[5]=mat4(vec4(-1.4109162,-0.372762,-3.770383,-21.367174),vec4(-6.2103205,-9.35908,0.92529047,8.82561),vec4(11.460242,-22.348068,13.625772,-18.693201),vec4(-0.3429052,-3.9905605,-2.4626114,-0.45033523))*buf[0]+mat4(vec4(7.3481627,-4.3661838,-6.3037653,-3.868115),vec4(1.5462853,6.5488915,1.9701879,-0.58291394),vec4(6.5858274,-2.2180402,3.7127688,-1.3730392),vec4(-5.7973905,10.134961,-2.3395722,-5.965605))*buf[1]+mat4(vec4(-2.5132585,-6.6685553,-1.4029363,-0.16285264),vec4(-0.37908727,0.53738135,4.389061,-1.3024765),vec4(-0.70647055,2.0111287,-5.1659346,-3.728635),vec4(-13.562562,10.487719,-0.9173751,-2.6487076))*buf[2]+mat4(vec4(-8.645013,6.5546675,-6.3944063,-5.5933375),vec4(-0.57783127,-1.077275,36.91025,5.736769),vec4(14.283112,3.7146652,7.1452246,-4.5958776),vec4(2.7192075,3.6021907,-4.366337,-2.3653464))*buf[3]+vec4(-5.9000807,-4.329569,1.2427121,8.59503);\n  buf[4]=sigmoid(buf[4]);buf[5]=sigmoid(buf[5]);\n  buf[6]=mat4(vec4(-1.61102,0.7970257,1.4675229,0.20917463),vec4(-28.793737,-7.1390953,1.5025433,4.656581),vec4(-10.94861,39.66238,0.74318546,-10.095605),vec4(-0.7229728,-1.5483948,0.7301322,2.1687684))*buf[0]+mat4(vec4(3.2547753,21.489103,-1.0194173,-3.3100595),vec4(-3.7316632,-3.3792162,-7.223193,-0.23685838),vec4(13.1804495,0.7916005,5.338587,5.687114),vec4(-4.167605,-17.798311,-6.815736,-1.6451967))*buf[1]+mat4(vec4(0.604885,-7.800309,-7.213122,-2.741014),vec4(-3.522382,-0.12359311,-0.5258442,0.43852118),vec4(9.6752825,-22.853785,2.062431,0.099892326),vec4(-4.3196306,-17.730087,2.5184598,5.30267))*buf[2]+mat4(vec4(-6.545563,-15.790176,-6.0438633,-5.415399),vec4(-43.591583,28.551912,-16.00161,18.84728),vec4(4.212382,8.394307,3.0958717,8.657522),vec4(-5.0237565,-4.450633,-4.4768,-5.5010443))*buf[3]+mat4(vec4(1.6985557,-67.05806,6.897715,1.9004834),vec4(1.8680354,2.3915145,2.5231109,4.081538),vec4(11.158006,1.7294737,2.0738268,7.386411),vec4(-4.256034,-306.24686,8.258898,-17.132736))*buf[4]+mat4(vec4(1.6889864,-4.5852966,3.8534803,-6.3482175),vec4(1.3543309,-1.2640043,9.932754,2.9079645),vec4(-5.2770967,0.07150358,-0.13962056,3.3269649),vec4(28.34703,-4.918278,6.1044083,4.085355))*buf[5]+vec4(6.6818056,12.522166,-3.7075126,-4.104386);\n  buf[7]=mat4(vec4(-8.265602,-4.7027016,5.098234,0.7509808),vec4(8.6507845,-17.15949,16.51939,-8.884479),vec4(-4.036479,-2.3946867,-2.6055532,-1.9866527),vec4(-2.2167742,-1.8135649,-5.9759874,4.8846445))*buf[0]+mat4(vec4(6.7790847,3.5076547,-2.8191125,-2.7028968),vec4(-5.743024,-0.27844876,1.4958696,-5.0517144),vec4(13.122226,15.735168,-2.9397483,-4.101023),vec4(-14.375265,-5.030483,-6.2599335,2.9848232))*buf[1]+mat4(vec4(4.0950394,-0.94011575,-5.674733,4.755022),vec4(4.3809423,4.8310084,1.7425908,-3.437416),vec4(2.117492,0.16342592,-104.56341,16.949184),vec4(-5.22543,-2.994248,3.8350096,-1.9364246))*buf[2]+mat4(vec4(-5.900337,1.7946124,-13.604192,-3.8060522),vec4(6.6583457,31.911177,25.164474,91.81147),vec4(11.840538,4.1503043,-0.7314397,6.768467),vec4(-6.3967767,4.034772,6.1714606,-0.32874924))*buf[3]+mat4(vec4(3.4992442,-196.91893,-8.923708,2.8142626),vec4(3.4806502,-3.1846354,5.1725626,5.1804223),vec4(-2.4009497,15.585794,1.2863957,2.0252278),vec4(-71.25271,-62.441242,-8.138444,0.50670296))*buf[4]+mat4(vec4(-12.291733,-11.176166,-7.3474145,4.390294),vec4(10.805477,5.6337385,-0.9385842,-4.7348723),vec4(-12.869276,-7.039391,5.3029537,7.5436664),vec4(1.4593618,8.91898,3.5101583,5.840625))*buf[5]+vec4(2.2415268,-6.705987,-0.98861027,-2.117676);\n  buf[6]=sigmoid(buf[6]);buf[7]=sigmoid(buf[7]);\n  buf[0]=mat4(vec4(1.6794263,1.3817469,2.9625452,0.),vec4(-1.8834411,-1.4806935,-3.5924516,0.),vec4(-1.3279216,-1.0918057,-2.3124623,0.),vec4(0.2662234,0.23235129,0.44178495,0.))*buf[0]+mat4(vec4(-0.6299101,-0.5945583,-0.9125601,0.),vec4(0.17828953,0.18300213,0.18182953,0.),vec4(-2.96544,-2.5819945,-4.9001055,0.),vec4(1.4195864,1.1868085,2.5176322,0.))*buf[1]+mat4(vec4(-1.2584374,-1.0552157,-2.1688404,0.),vec4(-0.7200217,-0.52666044,-1.438251,0.),vec4(0.15345335,0.15196142,0.272854,0.),vec4(0.945728,0.8861938,1.2766753,0.))*buf[2]+mat4(vec4(-2.4218085,-1.968602,-4.35166,0.),vec4(-22.683098,-18.0544,-41.954372,0.),vec4(0.63792,0.5470648,1.1078634,0.),vec4(-1.5489894,-1.3075932,-2.6444845,0.))*buf[3]+mat4(vec4(-0.49252132,-0.39877754,-0.91366625,0.),vec4(0.95609266,0.7923952,1.640221,0.),vec4(0.30616966,0.15693925,0.8639857,0.),vec4(1.1825981,0.94504964,2.176963,0.))*buf[4]+mat4(vec4(0.35446745,0.3293795,0.59547555,0.),vec4(-0.58784515,-0.48177817,-1.0614829,0.),vec4(2.5271258,1.9991658,4.6846647,0.),vec4(0.13042648,0.08864098,0.30187556,0.))*buf[5]+mat4(vec4(-1.7718065,-1.4033192,-3.3355875,0.),vec4(3.1664357,2.638297,5.378702,0.),vec4(-3.1724713,-2.6107926,-5.549295,0.),vec4(-2.851368,-2.249092,-5.3013067,0.))*buf[6]+mat4(vec4(1.5203838,1.2212278,2.8404984,0.),vec4(1.5210563,1.2651345,2.683903,0.),vec4(2.9789467,2.4364579,5.2347264,0.),vec4(2.2270417,1.8825914,3.8028636,0.))*buf[7]+vec4(-1.5468478,-3.6171484,0.24762098,0.);\n  buf[0]=sigmoid(buf[0]);\n  return vec4(buf[0].x,buf[0].y,buf[0].z,1.);\n}";
  const canvas = document.getElementById('veil-canvas');
  const gl = canvas.getContext('webgl2', { alpha: false, antialias: false, premultipliedAlpha: false, powerPreference: 'low-power' });
  if (!gl) return;
  const floatTargets = !!gl.getExtension('EXT_color_buffer_float');

  // The veil is soft, so it renders at half the window's CSS size and is stretched up; it redraws at
  // 30 fps while the editor has focus, and holds its last frame (no GPU work) while another window,
  // e.g. the game, is in front.
  const RENDER_SCALE = 0.5, FPS = 30;
  const SPEED = 0.5, FLOW = 1.0, GLOW = 0.4, GRAIN = 0.03, QUALITY = 0.5, INTRO = 1.8, MOUSE = 1.0;
  const lin = c => { c /= 255; return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
  const COLOR = [lin(0x6B), lin(0x12), lin(0xFF)];
  const SKY = [0x0A / 255, 0x0A / 255, 0x0A / 255];

  const vertex = `#version 300 es
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}`;

  // Field: veil.cpp ps_field, in the same y-down pixel frame (posDown).
  const field = `#version 300 es
precision highp float;
uniform vec2 uView; uniform vec2 uScale; uniform float uFieldH;
uniform float uZoom; uniform float uTime; uniform float uFlow; uniform float uWarp;
uniform vec2 uTurn; uniform float uUnfold;
uniform vec4 uPointer; uniform vec3 uSeed; uniform vec2 uDrag;
out vec4 outColor;
vec4 sigmoid(vec4 x) { return 1.0 / (1.0 + exp(-x)); }
%CPPN%
vec2 rotate(float a, vec2 v) { float c = cos(a), s = sin(a); return vec2(c * v.x - s * v.y, s * v.x + c * v.y); }
void main() {
  vec2 pos = vec2(gl_FragCoord.x, uFieldH - gl_FragCoord.y);
  vec2 css = pos * uScale;
  vec2 uv = (css * 2.0 - uView) / uView.y;
  vec2 anchor = vec2(uView.x / uView.y, -1.0);
  vec2 pointer = vec2(uPointer.x * 2.0 - uView.x, uPointer.y * 2.0 - uView.y) / uView.y;
  vec2 offset = uv - pointer;
  float touch = uPointer.z * exp(-dot(offset, offset) / (uPointer.w * uPointer.w));
  uv = pointer + rotate(touch * 0.6, offset);
  uv -= uDrag * touch;
  float t = uTime;
  uv = rotate(0.06 * uFlow * sin(t * 0.17), uv);
  uv += 0.05 * uFlow * vec2(sin(t * 0.21), cos(t * 0.13));
  uv = anchor + vec2(uTurn.x * uv.x - uTurn.y * uv.y, uTurn.y * uv.x + uTurn.x * uv.y) * 2.0 / uZoom;
  uv += uWarp * vec2(sin(uv.y * 6.283 + t * 0.5), cos(uv.x * 6.283 + t * 0.5)) * 0.05;
  vec3 latent = uSeed + 0.1 * uFlow * vec3(sin(0.3 * t), sin(0.69 * t), sin(0.44 * t));
  latent += touch * vec3(0.12, -0.1, 0.16);
  latent.z += uUnfold;
  outColor = vec4(cppn(uv, latent).rgb, 1.0);
}`;

  // Composite: veil.cpp ps_composite.
  const composite = `#version 300 es
precision highp float;
uniform sampler2D uField;
uniform vec2 uResolution; uniform vec2 uTexel;
uniform vec3 uColor; uniform float uGain; uniform float uGlowAmount; uniform float uPresence; uniform float uGrain;
uniform float uClock; uniform vec3 uLight; uniform vec3 uSky;
out vec4 outColor;
float hash2(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float level(vec2 uv, float lod) { return textureLod(uField, vec2(uv.x, 1.0 - uv.y), lod).b; }
void main() {
  vec2 pos = vec2(gl_FragCoord.x, uResolution.y - gl_FragCoord.y);
  vec2 uv = pos / uResolution;
  float base = clamp(level(uv, 0.0), 0.0, 1.0);
  float glow = level(uv, 2.0) * 0.4 + level(uv, 3.0) * 0.3 + level(uv, 4.0) * 0.2 + level(uv, 5.0) * 0.1;
  vec2 pixel = pos / uResolution.y;
  float near = uLight.z * exp(-dot(uLight.xy - pixel, uLight.xy - pixel) / 0.2);
  float energy = pow(base, 2.2) * uGain * (1.0 + near * 0.4);
  energy += pow(glow, 2.2) * uGlowAmount * uGain;
  energy *= uPresence;
  vec3 light = uColor * energy;
  light += uColor * energy * energy * 0.35;
  float grain = (hash2(pos + fract(uClock * 7.31) * 113.0) - 0.5) * uGrain;
  float dither = (hash2(pos.yx + 31.7) - 0.5) / 255.0;
  float peak = max(light.r, max(light.g, light.b));
  vec3 color = 1.0 - exp(-light);
  color = pow(color, vec3(1.0 / 2.2));
  color = clamp(color + grain * peak + dither, 0.0, 1.0);
  float alpha = max(color.r, max(color.g, color.b));
  outColor = vec4(color + uSky * (1.0 - alpha), 1.0);
}`;

  function shader(type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(s));
    return s;
  }
  function program(fs) {
    const p = gl.createProgram();
    gl.attachShader(p, shader(gl.VERTEX_SHADER, vertex));
    gl.attachShader(p, shader(gl.FRAGMENT_SHADER, fs));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw Error(gl.getProgramInfoLog(p));
    const u = {};
    const n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < n; i++) { const name = gl.getActiveUniform(p, i).name; u[name] = gl.getUniformLocation(p, name); }
    return { p, u };
  }

  try {
    const fieldProg = program(field.replace('%CPPN%', CPPN_SOURCE));
    const compProg = program(composite);
    const tex = gl.createTexture();
    const fbo = gl.createFramebuffer();
    let fw = 0, fh = 0, w = 0, h = 0;

    function targets() {
      const cw = Math.max(1, Math.round(innerWidth * RENDER_SCALE));
      const ch = Math.max(1, Math.round(innerHeight * RENDER_SCALE));
      if (cw === w && ch === h) return;
      w = cw; h = ch;
      canvas.width = w; canvas.height = h;
      fw = Math.max(1, Math.round(w * QUALITY));
      fh = Math.max(1, Math.round(h * QUALITY));
      gl.bindTexture(gl.TEXTURE_2D, tex);
      const levels = Math.floor(Math.log2(Math.max(fw, fh))) + 1;
      gl.texImage2D(gl.TEXTURE_2D, 0, floatTargets ? gl.RGBA16F : gl.RGBA8, fw, fh, 0, gl.RGBA, floatTargets ? gl.HALF_FLOAT : gl.UNSIGNED_BYTE, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAX_LEVEL, levels - 1);
      gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
    }

    // RenderInstance()'s animation state.
    const s = { clock: 0, intro: 0, x: innerWidth / 2, y: innerHeight / 2, dragX: 0, dragY: 0, touch: 0, lightX: 0.5, lightY: 0.15 };
    let mouse = null;
    addEventListener('pointermove', e => { mouse = { x: e.clientX, y: e.clientY }; }, { passive: true });
    document.addEventListener('pointerleave', () => { mouse = null; });
    const still = matchMedia('(prefers-reduced-motion: reduce)');
    let last = performance.now();

    function frame(now) {
      requestAnimationFrame(frame);
      if (document.hidden || (!document.hasFocus() && s.intro >= 1)) { last = now; return; }
      if (now - last < 1000 / FPS - 2) return;
      const dt = Math.min(Math.max((now - last) / 1000, 0), 0.15);
      last = now;
      targets();
      const vw = innerWidth, vh = innerHeight;

      s.clock += (still.matches ? 0 : dt) * SPEED;
      s.intro = Math.min(1, s.intro + dt / INTRO);
      const engaged = !!mouse && !still.matches;
      const px = engaged ? mouse.x : s.x, py = engaged ? mouse.y : s.y;
      if (engaged && s.touch < 0.01) { s.x = px; s.y = py; }
      const follow = 1 - Math.exp(-dt * 5.5);
      const prevX = s.x, prevY = s.y;
      s.x += (px - s.x) * follow;
      s.y += (py - s.y) * follow;
      const velX = (s.x - prevX) / Math.max(dt, 0.001), velY = (s.y - prevY) / Math.max(dt, 0.001);
      const dragBlend = 1 - Math.exp(-dt * 3);
      const clamp = (v, a, b) => Math.min(Math.max(v, a), b);
      s.dragX += (clamp(velX * 0.12, -120, 120) - s.dragX) * dragBlend;
      s.dragY += (clamp(-velY * 0.12, -120, 120) - s.dragY) * dragBlend;
      s.touch += ((engaged ? 1 : 0) - s.touch) * (1 - Math.exp(-dt * (engaged ? 1.8 : 1.0)));
      const lightFollow = 1 - Math.exp(-dt * 2.4);
      s.lightX += ((engaged ? px / vw : 0.5) - s.lightX) * lightFollow;
      s.lightY += ((engaged ? py / vh : 0.15) - s.lightY) * lightFollow;
      const i = s.intro;
      const presence = i * i * i * (i * (i * 6 - 15) + 10);

      // field pass
      gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
      gl.viewport(0, 0, fw, fh);
      gl.useProgram(fieldProg.p);
      const fu = fieldProg.u;
      gl.uniform2f(fu.uView, vw, vh);
      gl.uniform2f(fu.uScale, vw / fw, vh / fh);
      gl.uniform1f(fu.uFieldH, fh);
      gl.uniform1f(fu.uZoom, 1.0);
      gl.uniform1f(fu.uTime, s.clock);
      gl.uniform1f(fu.uFlow, FLOW);
      gl.uniform1f(fu.uWarp, 0.0);
      gl.uniform2f(fu.uTurn, 1.0, 0.0);
      gl.uniform1f(fu.uUnfold, (1 - presence) * 0.45);
      gl.uniform4f(fu.uPointer, s.x, s.y, s.touch * MOUSE * 0.9, 0.42);
      gl.uniform3f(fu.uSeed, 0.3948333106474662, 0.36, 0.14);
      gl.uniform2f(fu.uDrag, s.dragX * 2 / vh, s.dragY * 2 / vh);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.generateMipmap(gl.TEXTURE_2D);

      // composite pass
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, w, h);
      gl.useProgram(compProg.p);
      const cu = compProg.u;
      gl.uniform1i(cu.uField, 0);
      gl.uniform2f(cu.uResolution, w, h);
      gl.uniform2f(cu.uTexel, 1 / fw, 1 / fh);
      gl.uniform3f(cu.uColor, COLOR[0], COLOR[1], COLOR[2]);
      gl.uniform1f(cu.uGain, 1.15);
      gl.uniform1f(cu.uGlowAmount, GLOW);
      gl.uniform1f(cu.uPresence, presence);
      gl.uniform1f(cu.uGrain, GRAIN);
      gl.uniform1f(cu.uClock, s.clock % 1000);
      gl.uniform3f(cu.uLight, s.lightX * vw / vh, s.lightY, s.touch * MOUSE);
      gl.uniform3f(cu.uSky, SKY[0], SKY[1], SKY[2]);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
    requestAnimationFrame(frame);
    canvas.dataset.effect = 'darkveil';
  } catch (error) {
    console.error('Dark Veil:', error);
    canvas.remove();
  }
})();
