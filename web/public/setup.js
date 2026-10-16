// Setup wizard: talks to Tasky over Web Serial with the line protocol in
// firmware/tasky/usb_setup.h ("TASKY?", "TASKY SCAN", "TASKY SET {json}").
// Replies start with "@tasky "; everything else on the port is debug log.

let STEPS = ["connect", "wifi", "calendar", "tasks", "github", "ai", "place", "done"];
const MIN_VERSION = [2, 6, 0];

const $ = (id) => document.getElementById(id);
const wizard = $("wizard");

if (!("serial" in navigator)) {
  wizard.querySelector(".wz-card").hidden = true;
  wizard.querySelector(".wz-progress").hidden = true;
  $("wz-unsupported").hidden = false;
}

// ---------- Serial transport ----------
class TaskyLink {
  constructor(port) {
    this.port = port;
    this.waiters = [];
    this.buffer = "";
  }

  async open() {
    await this.port.open({ baudRate: 115200 });
    // Keep DTR/RTS low so opening the port doesn't reset the ESP32.
    await this.port.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => {});
    this.writer = this.port.writable.getWriter();
    this.readLoop();
  }

  async readLoop() {
    const reader = this.port.readable.pipeThrough(new TextDecoderStream()).getReader();
    this.reader = reader;
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        this.buffer += value;
        let nl;
        while ((nl = this.buffer.indexOf("\n")) >= 0) {
          const line = this.buffer.slice(0, nl).trim();
          this.buffer = this.buffer.slice(nl + 1);
          if (!line.startsWith("@tasky ")) continue;
          let msg;
          try { msg = JSON.parse(line.slice(7)); } catch { continue; }
          this.waiters.shift()?.resolve(msg);
        }
      }
    } catch { /* port closed / unplugged */ }
    for (const w of this.waiters.splice(0)) w.reject(new Error("Tasky disconnected"));
  }

  send(cmd, timeoutMs = 4000) {
    return new Promise((resolve, reject) => {
      const waiter = { resolve, reject };
      this.waiters.push(waiter);
      const timer = setTimeout(() => {
        const i = this.waiters.indexOf(waiter);
        if (i >= 0) this.waiters.splice(i, 1);
        reject(new Error("timeout"));
      }, timeoutMs);
      const done = (fn) => (v) => { clearTimeout(timer); fn(v); };
      waiter.resolve = done(resolve);
      waiter.reject = done(reject);
      this.writer.write(new TextEncoder().encode(cmd + "\n")).catch(reject);
    });
  }

  status() { return this.send("TASKY?"); }
  scan() { return this.send("TASKY SCAN", 15000); }
  set(obj) { return this.send("TASKY SET " + JSON.stringify(obj)); }
}

let link = null;
let state = null;

// ---------- Step navigation ----------
function show(step) {
  for (const el of wizard.querySelectorAll(".wz-step")) el.hidden = el.dataset.step !== step;
  const idx = STEPS.indexOf(step);
  for (const li of wizard.querySelectorAll(".wz-progress li")) {
    const i = STEPS.indexOf(li.dataset.step);
    li.classList.toggle("done", i < idx);
    li.classList.toggle("current", i === idx);
  }
  if (step === "wifi") scanNetworks();
  wizard.querySelector(`.wz-step[data-step="${step}"] input:not([type=radio])`)?.focus({ preventScroll: true });
}
const next = (from) => show(STEPS[Math.min(STEPS.indexOf(from) + 1, STEPS.length - 1)]);

for (const btn of wizard.querySelectorAll("[data-skip]")) {
  btn.addEventListener("click", () => next(btn.closest(".wz-step").dataset.step));
}

function setStatus(id, text, kind = "") {
  const el = $(id);
  el.textContent = text;
  el.className = "wz-status " + kind;
}

async function busy(btn, fn) {
  btn.disabled = true;
  try { await fn(); } finally { btn.disabled = false; }
}

// ---------- Connect ----------
const older = (v) => {
  const p = String(v).split(".").map(Number);
  for (let i = 0; i < 3; i++) if ((p[i] || 0) !== MIN_VERSION[i]) return (p[i] || 0) < MIN_VERSION[i];
  return false;
};

$("wz-connect").addEventListener("click", (e) => busy(e.currentTarget, async () => {
  try {
    const port = await navigator.serial.requestPort();
    link = new TaskyLink(port);
    await link.open();
  } catch (err) {
    if (err.name === "NotFoundError") return;  // user closed the picker
    setStatus("wz-connect-status", err.name === "InvalidStateError"
      ? "That port is busy. Close the installer window (or any serial monitor) and try again."
      : `Couldn't open the port: ${err.message}`, "error");
    return;
  }
  setStatus("wz-connect-status", "Saying hello…");
  // Tasky may still be booting right after flashing: retry for ~10 s.
  for (let i = 0; i < 10 && !state; i++) {
    try { state = await link.status(); } catch { await new Promise((r) => setTimeout(r, 1000)); }
  }
  if (!state) {
    setStatus("wz-connect-status", "No answer. Make sure Tasky is running firmware 2.6 or newer (flash it above), then reconnect.", "error");
    return;
  }
  if (older(state.v)) {
    setStatus("wz-connect-status", `Tasky is on v${state.v}. Flash the latest firmware above first.`, "error");
    return;
  }
  // The browser knows the time and zone: Tasky shows the clock even before Wi-Fi.
  await link.set({
    tz: Math.round(-new Date().getTimezoneOffset() / 60),
    time: Math.floor(Date.now() / 1000),
  }).catch(() => {});
  prefill(state);
  if (state.edition === "agents") {
    // The Agents edition has no on-device AI chat: skip that step, explain hooks instead
    STEPS = STEPS.filter((s) => s !== "ai");
    wizard.querySelector('.wz-progress li[data-step="ai"]').hidden = true;
    $("wz-agents-tip").hidden = false;
  }
  setStatus("wz-connect-status", `Connected to ${state.edition === "agents" ? "Tasky for Agents" : "Tasky"} v${state.v}.`, "ok");
  setTimeout(() => show("wifi"), 500);
}));

function prefill(s) {
  $("wz-ssid").value = s.wifi?.ssid || "";
  if (s.wifi?.connected) setStatus("wz-wifi-status", `Already connected to ${s.wifi.ssid}. Change it below or continue.`, "ok");
  $("wz-ghu").value = s.cfg?.ghu || "";
  $("wz-city").value = s.cfg?.city || "";
  (s.cfg?.tasks || []).forEach((t, i) => { if (t && t !== "Empty") $("wz-t" + i).value = t; });
  if (s.cfg?.cal) $("wz-cal").placeholder = "Saved. Paste a new link to replace it";
  if (s.cfg?.llmKey) $("wz-llmkey").placeholder = "Saved. Paste a new key to replace it";
  const prov = wizard.querySelector(`input[name="wz-llm"][value="${s.cfg?.llm ?? 1}"]`);
  if (prov) prov.checked = true;
}

// ---------- Wi-Fi ----------
async function scanNetworks() {
  const box = $("wz-networks");
  box.innerHTML = '<p class="small">Scanning…</p>';
  try {
    const { scan } = await link.scan();
    box.textContent = "";
    if (!scan.length) { box.innerHTML = '<p class="small">No networks found. Type the name below.</p>'; return; }
    for (const n of scan.sort((a, b) => b.r - a.r)) {
      const bars = n.r > -55 ? 4 : n.r > -67 ? 3 : n.r > -78 ? 2 : 1;
      const label = document.createElement("label");
      label.className = "wz-net";
      const radio = Object.assign(document.createElement("input"), { type: "radio", name: "wz-net", value: n.s });
      radio.checked = n.s === $("wz-ssid").value;
      radio.addEventListener("change", () => { $("wz-ssid").value = n.s; $("wz-pass").focus(); });
      const name = Object.assign(document.createElement("span"), { textContent: n.s });  // textContent: SSIDs are untrusted
      const meta = Object.assign(document.createElement("small"), { textContent: `${n.o ? "open" : "🔒"} ${"▂▄▆█".slice(0, bars)}` });
      label.append(radio, name, meta);
      box.append(label);
    }
  } catch {
    box.innerHTML = '<p class="small">Couldn\'t scan. Type the network name below.</p>';
  }
}
$("wz-rescan").addEventListener("click", (e) => busy(e.currentTarget, scanNetworks));

$("wz-wifi-save").addEventListener("click", (e) => busy(e.currentTarget, async () => {
  const ssid = $("wz-ssid").value.trim();
  if (!ssid) { setStatus("wz-wifi-status", "Pick or type a network.", "error"); return; }
  setStatus("wz-wifi-status", `Connecting to ${ssid}…`);
  await link.set({ ssid, pass: $("wz-pass").value });
  for (let i = 0; i < 20; i++) {
    await new Promise((r) => setTimeout(r, 1000));
    const s = await link.status().catch(() => null);
    if (s?.wifi?.connected) {
      setStatus("wz-wifi-status", `Connected ✓ (${s.wifi.ip})`, "ok");
      setTimeout(() => next("wifi"), 800);
      return;
    }
    if (s && /fail/i.test(s.wifi?.status || "")) break;
  }
  setStatus("wz-wifi-status", "Couldn't connect. Check the password (and that it's a 2.4 GHz network), then try again.", "error");
}));

// ---------- Simple steps ----------
const SAVERS = {
  calendar: () => {
    const cal = $("wz-cal").value.trim().replace(/^webcal:\/\//i, "https://");
    if (!cal) return null;
    if (!/^https?:\/\//i.test(cal)) throw new Error("That doesn't look like a link. It should start with https://");
    return { cal };
  },
  tasks: () => ({ tasks: [0, 1, 2].map((i) => $("wz-t" + i).value.trim()) }),
  github: () => ({ ghu: $("wz-ghu").value.trim().replace(/^@/, "") }),
  ai: () => {
    const key = $("wz-llmkey").value.trim();
    const llm = Number(wizard.querySelector('input[name="wz-llm"]:checked').value);
    return key ? { llm, llmKey: key } : { llm };
  },
  place: () => {
    const city = $("wz-city").value.trim();
    const out = city ? { city } : {};
    if (coords) Object.assign(out, coords);
    return Object.keys(out).length ? out : null;
  },
};

for (const btn of wizard.querySelectorAll("[data-save]")) {
  const step = btn.dataset.save;
  btn.addEventListener("click", () => busy(btn, async () => {
    const statusEl = btn.closest(".wz-step").querySelector(".wz-status");
    try {
      const payload = SAVERS[step]();
      if (payload) {
        const r = await link.set(payload);
        if (!r.ok) throw new Error(r.error || "Tasky didn't accept that");
      }
      next(step);
    } catch (err) {
      if (statusEl) { statusEl.textContent = err.message; statusEl.className = "wz-status error"; }
      else alert(err.message);
    }
  }));
}

// ---------- Weather location ----------
let coords = null;
$("wz-locate").addEventListener("click", () => {
  if (!navigator.geolocation) return;
  setStatus("wz-place-status", "Finding you…");
  navigator.geolocation.getCurrentPosition(
    (p) => {
      coords = { lat: +p.coords.latitude.toFixed(4), lon: +p.coords.longitude.toFixed(4) };
      if (!$("wz-city").value) $("wz-city").value = "My location";
      setStatus("wz-place-status", `Got it (${coords.lat}, ${coords.lon}).`, "ok");
    },
    () => setStatus("wz-place-status", "Location not available. Type your city instead.", "error"),
    { timeout: 10000 },
  );
});

show("connect");
