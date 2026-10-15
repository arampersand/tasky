// <tasky-device> — interactive CSS-3D model of Tasky with a live screen.
// Zero dependencies. Drag to rotate, click the screen or knob to change screen.
// Colours come from the firmware palette (ui.h / ui.cpp): C_TEAL 0x2E57 = #29CBBD.
//
// Usage:  <script type="module" src="/device/tasky-device.js"></script>
//         <tasky-device style="--size: 260px"></tasky-device>
// Attributes: screen="focus|tasks|stats|more" (pins one screen, disables cycling)

const TEAL = "#29CBBD";
const BG = "#050a14";
const MUTED = "#7b8494";
const SCREENS = ["focus", "tasks", "stats", "more"];
const CYCLE_MS = 4200;

const NOISE = `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='120' height='120'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='1.4' numOctaves='2' stitchTiles='stitch'/%3E%3CfeColorMatrix values='0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 0 0 0 .09 0'/%3E%3C/filter%3E%3Crect width='120' height='120' filter='url(%23n)'/%3E%3C/svg%3E")`;

const css = `
:host { --size: 260px; display: inline-block; width: calc(var(--size) * 2); height: calc(var(--size) * 1.6);
  touch-action: pan-y; user-select: none; -webkit-user-select: none; cursor: grab; }
:host(:active) { cursor: grabbing; }
.stage { width: 100%; height: 100%; display: grid; place-items: center; perspective: calc(var(--size) * 5); position: relative; }
.shadow { position: absolute; top: calc(50% + var(--size) * .5); width: calc(var(--size) * 1.25); height: calc(var(--size) * .22);
  background: radial-gradient(closest-side, rgba(0,0,0,.65), transparent); filter: blur(6px); }
.glow { position: absolute; inset: 0; pointer-events: none; opacity: 0; transition: opacity .8s;
  background: radial-gradient(calc(var(--size) * .9) calc(var(--size) * .55) at 50% 28%, rgba(70,130,255,.2), transparent 70%); }
.glow.on { opacity: 1; }
.cube { --w: var(--size); --h: calc(var(--size) * 1.08); --d: calc(var(--size) * .92); --r: calc(var(--size) * .1);
  --wi: calc(var(--w) - 2 * var(--r)); --hi: calc(var(--h) - 2 * var(--r)); --di: calc(var(--d) - 2 * var(--r));
  position: relative; width: 0; height: 0; transform-style: preserve-3d; }
.cube * { transform-style: preserve-3d; }
.face { position: absolute; backface-visibility: hidden; background-color: #121212; background-image: ${NOISE}; }
.front, .back { width: var(--wi); height: var(--hi); left: calc(var(--wi) / -2); top: calc(var(--hi) / -2); }
.front { transform: translateZ(calc(var(--d) / 2));
  background-image: ${NOISE}, linear-gradient(160deg, #1f1f1f 0%, #121212 55%, #0b0b0b 100%); }
.back { transform: rotateY(180deg) translateZ(calc(var(--d) / 2)); background-image: ${NOISE}, linear-gradient(200deg, #161616, #0a0a0a); }
.left, .right { width: var(--di); height: var(--hi); left: calc(var(--di) / -2); top: calc(var(--hi) / -2);
  background-image: ${NOISE}, repeating-linear-gradient(0deg, rgba(255,255,255,.025) 0 1px, transparent 1px 3px), linear-gradient(90deg, #0d0d0d, #090909); }
.right { transform: rotateY(90deg) translateZ(calc(var(--w) / 2)); }
.left { transform: rotateY(-90deg) translateZ(calc(var(--w) / 2)); }
.top, .bottom { width: var(--wi); height: var(--di); left: calc(var(--wi) / -2); top: calc(var(--di) / -2); }
.edge, .core { position: absolute; left: 0; top: 0; backface-visibility: hidden; }
.core { background: #0c0c0c; }
.top { transform: rotateX(90deg) translateZ(calc(var(--h) / 2));
  background-image: ${NOISE}, repeating-linear-gradient(90deg, rgba(255,255,255,.03) 0 1px, transparent 1px 3px), linear-gradient(#1c1c1c, #141414); }
.bottom { transform: rotateX(-90deg) translateZ(calc(var(--h) / 2)); background: #050505; }
.seam { position: absolute; left: 0; right: 0; top: 52%; height: 1px; background: rgba(0,0,0,.8); box-shadow: 0 1px 0 rgba(255,255,255,.05); }
.lightbar { position: absolute; left: 9%; right: 9%; top: 2%; height: 14%; border-radius: calc(var(--size) * .03);
  background: repeating-linear-gradient(90deg, rgba(255,255,255,.06) 0 2px, transparent 2px 5px), linear-gradient(#3d63a8, #24407a);
  box-shadow: inset 0 0 0 2px rgba(0,0,0,.3), inset 0 -6px 10px rgba(0,0,0,.25); transition: box-shadow .6s, background .6s; }
.lightbar.on { background: repeating-linear-gradient(90deg, rgba(255,255,255,.18) 0 2px, transparent 2px 5px), linear-gradient(#9cc4ff, #3f7dff);
  box-shadow: 0 0 calc(var(--size) * .14) rgba(70,130,255,.7), 0 0 calc(var(--size) * .04) rgba(150,190,255,.9), inset 0 0 0 2px rgba(200,225,255,.45); }
.bezel { position: absolute; left: 12%; width: 76%; top: 24%; aspect-ratio: 1 / 1.04; border-radius: calc(var(--size) * .02);
  background: #000; padding: 3.5% 3.5% 6%; box-sizing: border-box; box-shadow: inset 0 0 0 1px #1d1d1d, 0 0 0 2px #0a0a0a; }
canvas { width: 100%; aspect-ratio: 1; display: block; border-radius: 2px; cursor: pointer;
  box-shadow: 0 0 calc(var(--size) * .05) rgba(41,206,189,.12); }
.glare { position: absolute; inset: 0; border-radius: inherit; pointer-events: none;
  background: linear-gradient(125deg, rgba(255,255,255,.10) 0%, rgba(255,255,255,.02) 35%, transparent 36%); }
.engrave { position: absolute; inset: 0; display: grid; place-items: center; font: italic 600 calc(var(--size) * .2)/1 "Segoe Script", "Bradley Hand", cursive;
  color: #0a0a0a; text-shadow: 0 1px 0 rgba(255,255,255,.06); transform: rotate(-8deg); }
.rib, .cap, .brib, .bcap { position: absolute; left: 0; top: 0; }
.rib { width: var(--kl); height: var(--ks); margin: calc(var(--ks) / -2) 0 0 calc(var(--kl) / -2);
  background: linear-gradient(90deg, #070707, #1b1b1b 40%, #0e0e0e); border-left: 1px solid #000; }
.rib:nth-child(odd) { background: linear-gradient(90deg, #050505, #141414 40%, #0a0a0a); }
.cap { width: calc(var(--kr) * 2); height: calc(var(--kr) * 2); margin: calc(var(--kr) * -1) 0 0 calc(var(--kr) * -1); border-radius: 50%;
  background-image: ${NOISE}, radial-gradient(circle at 40% 35%, #262626, #101010 70%); box-shadow: inset 0 0 0 3px #0a0a0a, inset 0 0 0 5px #1a1a1a; cursor: pointer; }
.brib { width: var(--bs); height: var(--bh); margin: calc(var(--bh) / -2) 0 0 calc(var(--bs) / -2); background: #0d0d0d; }
.bcap { width: calc(var(--br) * 2); height: calc(var(--br) * 2); margin: calc(var(--br) * -1) 0 0 calc(var(--br) * -1); border-radius: 50%;
  background-image: ${NOISE}, radial-gradient(circle at 40% 35%, #2a2a2a, #111); }
.screw { position: absolute; width: 7%; aspect-ratio: 1; border-radius: 50%; left: 62%; top: 30%;
  background: radial-gradient(circle, #1a1a1a 30%, #050505 70%); box-shadow: inset 0 0 0 1px #000; }
.usb { position: absolute; width: 22%; height: 7%; left: 39%; bottom: 6%; border-radius: 3px; background: #020202; box-shadow: inset 0 0 0 1px #1d1d1d; }
@media (prefers-reduced-motion: reduce) { .lightbar, .glow { transition: none; } }
`;

class TaskyDevice extends HTMLElement {
  connectedCallback() {
    if (this.shadowRoot) return;
    const root = this.attachShadow({ mode: "open" });
    root.innerHTML = `<style>${css}</style>
      <div class="stage" part="stage">
        <div class="glow"></div><div class="shadow"></div>
        <div class="cube">
          <div class="face front"><div class="lightbar"></div>
            <div class="bezel"><canvas width="480" height="480" aria-label="Tasky screen"></canvas><div class="glare"></div></div></div>
          <div class="face back"><div class="engrave">Tasky</div></div>
          <div class="face right"><div class="seam"></div></div>
          <div class="face left"><div class="seam"></div><div class="usb"></div></div>
          <div class="face top"><div class="screw"></div></div>
          <div class="face bottom"></div>
          <div class="body"></div><div class="knob"></div><div class="button"></div>
        </div>
      </div>`;
    this.$ = (s) => root.querySelector(s);
    this.canvas = this.$("canvas");
    this.ctx = this.canvas.getContext("2d");
    this.ctx.scale(2, 2); // draw in the firmware's 240x240 coordinate space
    this.reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;
    this.pinned = this.getAttribute("screen");
    this.idx = Math.max(0, SCREENS.indexOf(this.pinned ?? "focus"));
    this.screenAt = performance.now();
    this.rot = { x: -14, y: -32 };
    this.drag = null;
    this.resizer = new ResizeObserver(() => this.buildCylinders());
    this.resizer.observe(this);
    this.bindInput();
    this.raf = requestAnimationFrame((t) => this.frame(t));
  }

  disconnectedCallback() { cancelAnimationFrame(this.raf); this.resizer?.disconnect(); }

  // Flat shading for generated slats: light from top-left-front.
  shade(nx, ny, nz, base = 9, gain = 28) {
    const l = [-.35, -.75, .56], len = Math.hypot(...l);
    const v = Math.round(base + gain * Math.max(0, (nx * l[0] + ny * l[1] + nz * l[2]) / len));
    return `rgb(${v},${v},${v})`;
  }

  slat(w, h, transform, color) {
    return `<div class="edge" style="width:${w}px;height:${h}px;margin:${-h / 2}px 0 0 ${-w / 2}px;transform:${transform};background:${color}"></div>`;
  }

  buildCylinders() {
    // --size may be any CSS length (e.g. min(240px, 34vw)); the host is 2 × size wide.
    const size = this.getBoundingClientRect().width / 2;
    if (!size || size === this.builtSize) return;
    this.builtSize = size;
    const W = size, H = size * 1.08, D = size * .92;
    this.$(".body").innerHTML = this.roundedEdges(W, H, D, size * .1);
    const rad = Math.PI / 180;

    // Encoder knob on the right side: axis along X, rounded outer rim.
    const kr = H * .26, kl = W * .15, kb = kr * .22, n = 32;
    const knob = this.$(".knob");
    knob.style.cssText = `--kr:${kr - kb}px;--kl:${kl - kb}px;--ks:${(2 * Math.PI * kr) / n + 1}px`;
    const ky = H * .1, kz = D * -.02;
    let html = "";
    for (let i = 0; i < n; i++) {
      const th = (360 / n) * i;
      html += `<div class="rib" style="--kr:${kr}px;transform:translate3d(${W / 2 + (kl - kb) / 2}px,${ky}px,${kz}px) rotateX(${th}deg) translateZ(${kr}px)"></div>`;
      for (let j = 1; j <= 3; j++) {
        const a = j * 22.5, ar = a * rad, rho = kr - kb + kb * Math.cos(ar), x = W / 2 + kl - kb + kb * Math.sin(ar);
        const ny = -Math.sin(th * rad) * Math.cos(ar), nz = Math.cos(th * rad) * Math.cos(ar), nx = Math.sin(ar);
        html += this.slat((kb * Math.PI) / 6 + .8, (2 * Math.PI * rho) / n + 1,
          `translate3d(${x}px,${ky}px,${kz}px) rotateX(${th}deg) translateZ(${rho}px) rotateY(${a}deg)`, this.shade(nx, ny, nz, 7, 22));
      }
    }
    html += `<div class="cap" style="transform:translate3d(${W / 2 + kl}px,${ky}px,${kz}px) rotateY(90deg)"></div>`;
    knob.innerHTML = html;

    // MODE button on top: axis along Y, rounded top rim.
    const br = W * .065, bh = H * .06, bb = br * .35, m = 20;
    const btn = this.$(".button");
    btn.style.cssText = `--br:${br - bb}px;--bh:${bh - bb}px;--bs:${(2 * Math.PI * br) / m + 1}px`;
    const bx = W * .02, bz = D * .12, top = -H / 2;
    html = "";
    for (let i = 0; i < m; i++) {
      const th = (360 / m) * i;
      html += `<div class="brib" style="transform:translate3d(${bx}px,${top - (bh - bb) / 2}px,${bz}px) rotateY(${th}deg) translateZ(${br}px)"></div>`;
      for (let j = 1; j <= 3; j++) {
        const a = j * 22.5, ar = a * rad, rho = br - bb + bb * Math.cos(ar), y = top - bh + bb - bb * Math.sin(ar);
        html += this.slat((2 * Math.PI * rho) / m + 1, (bb * Math.PI) / 6 + .8,
          `translate3d(${bx}px,${y}px,${bz}px) rotateY(${th}deg) translateZ(${rho}px) rotateX(${a}deg)`,
          this.shade(Math.sin(th * rad) * Math.cos(ar), -Math.sin(ar), Math.cos(th * rad) * Math.cos(ar)));
      }
    }
    html += `<div class="bcap" style="transform:translate3d(${bx}px,${top - bh}px,${bz}px) rotateX(90deg)"></div>`;
    btn.innerHTML = html;
  }

  // 12 quarter-cylinder edges joining the inset faces, plus a dark core box
  // that fills the small gaps left at the 8 corners.
  roundedEdges(W, H, D, R) {
    const hx = W / 2 - R, hy = H / 2 - R, hz = D / 2 - R, n = 6, s = (R * Math.PI) / 2 / n + .8;
    const rad = Math.PI / 180, signs = [-1, 1];
    let html = "";
    const arc = (center, fn) => { for (let k = 0; k < n; k++) fn(center - 45 + ((k + .5) * 90) / n); };
    for (const a of signs) for (const b of signs) {
      // Along X (sy = a, sz = b)
      arc(Math.atan2(-a, b) / rad, (t) => html += this.slat(2 * hx, s,
        `translate3d(0,${a * hy}px,${b * hz}px) rotateX(${t}deg) translateZ(${R}px)`, this.shade(0, -Math.sin(t * rad), Math.cos(t * rad))));
      // Along Y (sx = a, sz = b)
      arc(Math.atan2(a, b) / rad, (t) => html += this.slat(s, 2 * hy,
        `translate3d(${a * hx}px,0,${b * hz}px) rotateY(${t}deg) translateZ(${R}px)`, this.shade(Math.sin(t * rad), 0, Math.cos(t * rad))));
      // Along Z (sx = a, sy = b)
      arc(Math.atan2(b, a) / rad, (t) => html += this.slat(2 * hz, s,
        `translate3d(${a * hx}px,${b * hy}px,0) rotateZ(${t}deg) rotateY(90deg) translateZ(${R}px)`, this.shade(Math.cos(t * rad), Math.sin(t * rad), 0)));
    }
    // Corners: 3x3 patches approximating a sphere octant.
    const g = 3, step = 90 / g;
    for (const sx of signs) for (const sy of signs) for (const sz of signs) {
      const phiC = Math.atan2(sx, sz) / rad;
      for (let i = 0; i < g; i++) for (let j = 0; j < g; j++) {
        const phi = phiC - 45 + (i + .5) * step, th = -sy * (j + .5) * step;
        const cp = Math.cos(th * rad), w = R * step * rad * cp + 1.2, h = R * step * rad + 1.2;
        html += this.slat(w, h, `translate3d(${sx * hx}px,${sy * hy}px,${sz * hz}px) rotateY(${phi}deg) rotateX(${th}deg) translateZ(${R}px)`,
          this.shade(cp * Math.sin(phi * rad), -Math.sin(th * rad), cp * Math.cos(phi * rad)));
      }
    }
    const c = R * .65, ex = hx + c, ey = hy + c, ez = hz + c;
    const core = (w, h, tf, nx, ny, nz) => html += `<div class="core" style="width:${w}px;height:${h}px;margin:${-h / 2}px 0 0 ${-w / 2}px;transform:${tf};background:${this.shade(nx, ny, nz, 6, 20)}"></div>`;
    core(2 * ex, 2 * ey, `translateZ(${ez}px)`, 0, 0, 1);
    core(2 * ex, 2 * ey, `rotateY(180deg) translateZ(${ez}px)`, 0, 0, -1);
    core(2 * ez, 2 * ey, `rotateY(90deg) translateZ(${ex}px)`, 1, 0, 0);
    core(2 * ez, 2 * ey, `rotateY(-90deg) translateZ(${ex}px)`, -1, 0, 0);
    core(2 * ex, 2 * ez, `rotateX(90deg) translateZ(${ey}px)`, 0, -1, 0);
    core(2 * ex, 2 * ez, `rotateX(-90deg) translateZ(${ey}px)`, 0, 1, 0);
    return html;
  }

  bindInput() {
    const next = (e) => { e.stopPropagation(); this.next(); };
    this.canvas.addEventListener("click", next);
    this.$(".knob").addEventListener("click", next);
    this.addEventListener("pointerdown", (e) => {
      this.drag = { x: e.clientX, y: e.clientY, rx: this.rot.x, ry: this.rot.y, moved: false };
      this.setPointerCapture(e.pointerId);
    });
    this.addEventListener("pointermove", (e) => {
      if (!this.drag) return;
      const dx = e.clientX - this.drag.x, dy = e.clientY - this.drag.y;
      if (Math.abs(dx) + Math.abs(dy) > 4) this.drag.moved = true;
      this.rot.y = this.drag.ry + dx * .5;
      this.rot.x = Math.max(-60, Math.min(40, this.drag.rx - dy * .4));
    });
    const end = () => { this.drag = null; this.releasedAt = performance.now(); };
    this.addEventListener("pointerup", end);
    this.addEventListener("pointercancel", end);
  }

  next() {
    this.idx = (this.idx + 1) % SCREENS.length;
    this.screenAt = performance.now();
  }

  frame(t) {
    // Idle: ease back to the hero angle and sway gently.
    if (!this.drag) {
      const since = t - (this.releasedAt ?? -1e9);
      if (since > 1800) {
        const sway = this.reduced ? 0 : Math.sin(t / 2600) * 10;
        this.rot.y += (-32 + sway - this.rot.y) * .035;
        this.rot.x += (-14 - this.rot.x) * .035;
      }
    }
    this.$(".cube").style.transform = `rotateX(${this.rot.x}deg) rotateY(${this.rot.y}deg)`;
    if (!this.pinned && !this.reduced && t - this.screenAt > CYCLE_MS) this.next();
    this.draw(t);
    this.raf = requestAnimationFrame((tt) => this.frame(tt));
  }

  // ---------- Screen rendering (240x240, mirrors the firmware layouts) ----------
  draw(t) {
    const c = this.ctx, name = SCREENS[this.idx];
    const k = Math.min(1, (t - this.screenAt) / 380);
    const ease = 1 - Math.pow(1 - k, 3);
    c.save();
    c.fillStyle = BG; c.fillRect(0, 0, 240, 240);
    c.globalAlpha = ease;
    c.translate(0, (1 - ease) * 18); // firmware "enter" animation: slide up
    c.textAlign = "center"; c.textBaseline = "middle";
    this[name](c, t);
    c.restore();
    const running = name === "focus";
    this.$(".lightbar").classList.toggle("on", running);
    this.$(".glow").classList.toggle("on", running);
  }

  font(px, w = 700) { return `${w} ${px}px Inter, "Helvetica Neue", Arial, sans-serif`; }

  header(c, title) {
    c.fillStyle = "#fff"; c.font = this.font(22, 800); c.fillText(title, 120, 30);
    c.strokeStyle = "#5d6b80"; c.lineWidth = 1.5;
    c.beginPath(); c.moveTo(40, 48); c.lineTo(200, 48); c.stroke();
  }

  focus(c, t) {
    const elapsed = (t - this.screenAt) / 1000;
    const total = 25 * 60, left = Math.max(0, total - Math.floor(elapsed * 9)); // fast-forward for the demo
    const mm = String(Math.floor(left / 60)).padStart(2, "0"), ss = String(left % 60).padStart(2, "0");
    // Rounded-square progress ring, drains counter-clockwise like the firmware.
    const r = 30, x = 14, y = 14, s = 212;
    c.lineWidth = 4; c.strokeStyle = "rgba(41,206,189,.18)";
    this.rrect(c, x, y, s, s, r); c.stroke();
    c.save(); c.strokeStyle = TEAL; c.shadowColor = TEAL; c.shadowBlur = 8;
    const per = 4 * (s - 2 * r) + 2 * Math.PI * r;
    c.setLineDash([per * (left / total), per]);
    this.rrect(c, x, y, s, s, r); c.stroke(); c.restore();
    c.fillStyle = TEAL; c.font = this.font(17, 500); c.fillText("FOCUS", 120, 60);
    c.fillStyle = "#fff"; c.font = this.font(52, 800); c.fillText(`${mm}:${ss}`, 120, 118);
    c.fillStyle = "#e5e7eb"; c.font = this.font(19, 800); c.fillText("RUNNING", 120, 156);
    for (let i = 0; i < 4; i++) {
      c.beginPath(); c.arc(99 + i * 14, 182, 4, 0, Math.PI * 2);
      if (i < 2) { c.fillStyle = TEAL; c.fill(); } else { c.strokeStyle = MUTED; c.lineWidth = 1.5; c.stroke(); }
    }
  }

  tasks(c) {
    this.header(c, "TASKS");
    const items = [["Ship v2.4.0", true], ["Write the docs", true], ["Flash from web", false]];
    items.forEach(([label, done], i) => {
      const y = 82 + i * 40, sel = i === 2;
      if (sel) { c.fillStyle = TEAL; this.rrect(c, 22, y - 16, 196, 32, 6); c.fill(); }
      c.strokeStyle = sel ? BG : TEAL; c.lineWidth = 2;
      this.rrect(c, 32, y - 8, 16, 16, 3); c.stroke();
      if (done) { c.beginPath(); c.moveTo(35, y); c.lineTo(39, y + 4); c.lineTo(45, y - 4); c.stroke(); }
      c.textAlign = "left"; c.fillStyle = sel ? BG : done ? MUTED : "#fff"; c.font = this.font(17, 700);
      c.fillText(label, 58, y + 1);
      c.textAlign = "center";
    });
    c.fillStyle = MUTED; c.font = this.font(12, 600); c.fillText("2 / 3 done today", 120, 212);
  }

  stats(c) {
    this.header(c, "STATS");
    c.fillStyle = "#fff"; c.font = this.font(40, 800); c.fillText("0h 55m", 120, 98);
    c.font = this.font(15, 500); c.fillText("Total Focus Time", 120, 128);
    c.fillStyle = TEAL; c.font = this.font(20, 700); c.fillText("2 sessions today", 120, 166);
    c.fillStyle = "#d1d5db"; c.font = this.font(14, 500); c.fillText("55 min focused today", 120, 192);
  }

  more(c, t) {
    this.header(c, "MORE");
    const items = ["Focus", "Calendar", "Streak", "Weather", "Agent", "Update"];
    const sel = Math.floor((t - this.screenAt) / 900) % 3;
    items.forEach((label, i) => {
      const y = 70 + i * 26;
      if (i === sel) { c.fillStyle = TEAL; c.fillRect(24, y - 12, 192, 24); }
      c.textAlign = "left"; c.fillStyle = i === sel ? BG : "#fff"; c.font = this.font(17, i === sel ? 500 : 700);
      c.fillText(label, 34, y + 1);
      c.textAlign = "right"; c.font = this.font(13, 700); if (i < 5) c.fillText(">", 208, y + 1);
      c.textAlign = "center";
    });
    c.fillStyle = "#fff"; c.beginPath(); c.moveTo(114, 224); c.lineTo(126, 224); c.lineTo(120, 231); c.fill();
  }

  rrect(c, x, y, w, h, r) {
    c.beginPath();
    c.moveTo(x + w / 2, y); c.arcTo(x + w, y, x + w, y + h, r); c.arcTo(x + w, y + h, x, y + h, r);
    c.arcTo(x, y + h, x, y, r); c.arcTo(x, y, x + w, y, r); c.closePath();
  }
}

customElements.define("tasky-device", TaskyDevice);
