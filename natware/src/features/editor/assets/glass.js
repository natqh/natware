// React Bits "Glass Surface", dark scheme: the same component the external menu ports to HLSL
// (ui/veil.cpp, veil::Glass). Each pane bends the Dark Veil behind it through its own SVG
// displacement filter, sized to the pane, with the external menu's settings: GlassSurface.jsx
// defaults, frost (backgroundOpacity) per pane, and none of the component's white inset rims.
"use strict";
const Glass = (() => {
  const NS = 'http://www.w3.org/2000/svg';
  const settings = { borderWidth: 0.07, brightness: 50, opacity: 0.93, blur: 11, distortion: -180, offsets: [0, 10, 20] };
  let defs = null, next = 0;
  const panes = new Map();

  function node(name, attrs, parent) {
    const n = document.createElementNS(NS, name);
    for (const k in attrs) n.setAttribute(k, attrs[k]);
    if (parent) parent.append(n);
    return n;
  }

  function supported() {
    const probe = document.createElement('div');
    probe.style.backdropFilter = 'url(#glass-probe)';
    return probe.style.backdropFilter !== '';
  }

  // GlassSurface.jsx's displacement map: red fades in across, blue down (difference-blended),
  // and a blurred 50% grey slab inset by borderWidth flattens the middle so only the rim bends.
  function map(w, h, r) {
    const edge = Math.min(w, h) * settings.borderWidth * 0.5;
    const svg = `<svg viewBox="0 0 ${w} ${h}" xmlns="${NS}"><defs>`
      + `<linearGradient id="r" x1="100%" y1="0%" x2="0%" y2="0%"><stop offset="0%" stop-color="#0000"/><stop offset="100%" stop-color="red"/></linearGradient>`
      + `<linearGradient id="b" x1="0%" y1="0%" x2="0%" y2="100%"><stop offset="0%" stop-color="#0000"/><stop offset="100%" stop-color="blue"/></linearGradient></defs>`
      + `<rect x="0" y="0" width="${w}" height="${h}" fill="black"/>`
      + `<rect x="0" y="0" width="${w}" height="${h}" rx="${r}" fill="url(#r)"/>`
      + `<rect x="0" y="0" width="${w}" height="${h}" rx="${r}" fill="url(#b)" style="mix-blend-mode:difference"/>`
      + `<rect x="${edge}" y="${edge}" width="${Math.max(0, w - edge * 2)}" height="${Math.max(0, h - edge * 2)}" rx="${r}" `
      + `fill="hsl(0 0% ${settings.brightness}% / ${settings.opacity})" style="filter:blur(${settings.blur}px)"/></svg>`;
    return 'data:image/svg+xml,' + encodeURIComponent(svg);
  }

  function filterFor(id) {
    if (!defs) {
      const host = node('svg', { width: 0, height: 0, 'aria-hidden': 'true', focusable: 'false' });
      host.style.cssText = 'position:absolute;width:0;height:0;overflow:hidden';
      defs = node('defs', {}, host);
      document.body.prepend(host);
    }
    const f = node('filter', { id, 'color-interpolation-filters': 'sRGB', x: '0%', y: '0%', width: '100%', height: '100%' }, defs);
    const image = node('feImage', { x: 0, y: 0, width: '100%', height: '100%', preserveAspectRatio: 'none', result: 'map' }, f);
    const channels = ['red', 'green', 'blue'];
    channels.forEach((c, i) => {
      node('feDisplacementMap', { in: 'SourceGraphic', in2: 'map', result: 'disp' + c, scale: settings.distortion + settings.offsets[i], xChannelSelector: 'R', yChannelSelector: 'G' }, f);
      const m = ['0 0 0 0 0', '0 0 0 0 0', '0 0 0 0 0', '0 0 0 1 0'];
      m[i] = ['1 0 0 0 0', '0 1 0 0 0', '0 0 1 0 0'][i];
      node('feColorMatrix', { in: 'disp' + c, type: 'matrix', values: m.join(' '), result: c }, f);
    });
    node('feBlend', { in: 'red', in2: 'green', mode: 'screen', result: 'rg' }, f);
    node('feBlend', { in: 'rg', in2: 'blue', mode: 'screen' }, f);
    return image;
  }

  const resize = new ResizeObserver(entries => {
    for (const e of entries) {
      const pane = panes.get(e.target);
      if (!pane) continue;
      const w = Math.max(1, Math.round(e.target.offsetWidth)), h = Math.max(1, Math.round(e.target.offsetHeight));
      if (w === pane.w && h === pane.h) continue;
      pane.w = w; pane.h = h;
      pane.image.setAttribute('href', map(w, h, pane.radius));
    }
  });

  const enabled = supported();
  document.documentElement.classList.toggle('glass-svg', enabled);

  function attach(el) {
    if (panes.has(el)) return;
    const radius = Number(el.dataset.radius || 12);
    el.style.setProperty('--frost', el.dataset.frost || '0');
    if (!enabled) { panes.set(el, null); return; }
    const id = 'glass-' + (++next);
    const pane = { image: filterFor(id), radius, w: 0, h: 0 };
    panes.set(el, pane);
    el.style.setProperty('--glass-filter', `url(#${id})`);
    resize.observe(el);
  }

  function scan(root = document) {
    root.querySelectorAll('.glass').forEach(attach);
  }

  return { attach, scan, enabled };
})();
