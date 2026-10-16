// Setup wizard: talks to Tasky over Web Serial with the line protocol in
// firmware/tasky/usb_setup.h ("TASKY?", "TASKY SCAN", "TASKY SET {json}").
// Replies start with "@tasky "; everything else on the port is debug log.

let STEPS = ["connect", "wifi", "calendar", "tasks", "github", "ai", "place", "screens", "phrases", "light", "bluetooth", "agents", "done"];
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
    const decoder = new TextDecoderStream();
    this.piped = this.port.readable.pipeTo(decoder.writable).catch(() => {});  // close() waits on it
    const reader = decoder.readable.getReader();
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
          // Replies come in order. A request that timed out still owns its
          // late reply, so it can't be taken for the next command's answer.
          const w = this.waiters.shift();
          if (w && !w.abandoned) w.resolve(msg);
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
        waiter.abandoned = true;  // stays queued: its reply may still arrive
        reject(new Error("timeout"));
      }, timeoutMs);
      const done = (fn) => (v) => { clearTimeout(timer); fn(v); };
      waiter.resolve = done(resolve);
      waiter.reject = done(reject);
      this.writer.write(new TextEncoder().encode(cmd + "\n")).catch(reject);
    });
  }

  async close() {
    try { await this.reader?.cancel(); } catch { /* already gone */ }
    await this.piped;  // the port's readable stream is unlocked once the pipe ends
    try { this.writer.releaseLock(); } catch { /* already released */ }
    await this.port.close().catch(() => {});
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
  if (step === "github" && $("wz-ghu").value.trim()) lookupGitHub();
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
const older = (v, min = MIN_VERSION) => {
  const p = String(v).split(".").map(Number);
  for (let i = 0; i < 3; i++) if ((p[i] || 0) !== min[i]) return (p[i] || 0) < min[i];
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
  if (!state.cfg?.screenChoices) {
    // Firmware older than 2.11 has a fixed carousel and built-in phrases
    STEPS = STEPS.filter((s) => s !== "screens" && s !== "phrases");
    for (const id of ["screens", "phrases"]) wizard.querySelector(`.wz-progress li[data-step="${id}"]`).hidden = true;
  }
  if (state.cfg?.light === undefined) {
    // Firmware older than 2.8 has a single light type
    STEPS = STEPS.filter((s) => s !== "light");
    wizard.querySelector('.wz-progress li[data-step="light"]').hidden = true;
  }
  // Claude Code cards: Tasky 3.0+ reports "agents":true; 2.7–2.13 Agents builds report edition "agents".
  const oldAgents = state.edition === "agents";
  const hasAgents = state.agents === true || oldAgents;
  // Bluetooth keys: every Tasky 3.0+ (the 2.x Agents builds had them too)
  if (!hasAgents) STEPS = STEPS.filter((s) => s !== "agents" && s !== "bluetooth");
  else {
    wizard.querySelector('.wz-progress li[data-step="agents"]').hidden = false;
    wizard.querySelector('.wz-progress li[data-step="bluetooth"]').hidden = false;
    $("wz-agents-tip").hidden = false;
  }
  if (oldAgents) {
    // The 2.x Agents builds have no on-device AI chat (3.0 has both again)
    STEPS = STEPS.filter((s) => s !== "ai");
    wizard.querySelector('.wz-progress li[data-step="ai"]').hidden = true;
  }
  setStatus("wz-connect-status", `Connected to ${oldAgents ? "Tasky for Agents" : "Tasky"} v${state.v}.`, "ok");
  setTimeout(() => show("wifi"), 500);
}));

function prefill(s) {
  $("wz-wifi-intro").hidden = Boolean(s.wifi?.ssid);  // first time: a friendly hello
  $("wz-ssid").value = s.wifi?.ssid || "";
  if (s.wifi?.connected) setStatus("wz-wifi-status", `Already connected to ${s.wifi.ssid}. Change it below or continue.`, "ok");
  $("wz-ghu").value = s.cfg?.ghu || "";
  $("wz-city").value = s.cfg?.city || "";
  (s.cfg?.tasks || []).forEach((t, i) => { if (t && t !== "Empty") $("wz-t" + i).value = t; });
  if (s.cfg?.cal) $("wz-cal").placeholder = "Saved. Paste a new link to replace it";
  if (s.cfg?.llmKey) $("wz-llmkey").placeholder = "Saved. Paste a new key to replace it";
  const prov = wizard.querySelector(`input[name="wz-llm"][value="${s.cfg?.llm ?? 1}"]`);
  if (prov) prov.checked = true;
  if (s.cfg?.screenChoices) renderScreens(s.cfg.screenChoices, s.cfg.screens || []);
  $("wz-phrases").value = (s.cfg?.phrases || []).join("\n");
  const light = wizard.querySelector(`input[name="wz-light"][value="${s.cfg?.light ?? 0}"]`);
  if (light) light.checked = true;
}

// ---------- Screens ----------
// Picked screens first (in their order), then the rest unticked. ↑/↓ reorder.
function renderScreens(choices, picked) {
  const box = $("wz-screens");
  box.textContent = "";
  const ids = [...picked, ...choices.map((c) => c.id).filter((id) => !picked.includes(id))];
  for (const id of ids) {
    const choice = choices.find((c) => c.id === id);
    if (!choice) continue;
    const li = Object.assign(document.createElement("li"), { className: "wz-screen" });
    li.dataset.id = id;
    const label = document.createElement("label");
    const box_ = Object.assign(document.createElement("input"), { type: "checkbox", checked: picked.includes(id) });
    label.append(box_, Object.assign(document.createElement("span"), { textContent: choice.label }));
    const move = (dir, text) => {
      const b = Object.assign(document.createElement("button"), { type: "button", className: "wz-move", textContent: text });
      b.setAttribute("aria-label", `Move ${choice.label} ${dir}`);
      b.addEventListener("click", () => {
        if (dir === "up" && li.previousElementSibling) box.insertBefore(li, li.previousElementSibling);
        if (dir === "down" && li.nextElementSibling) box.insertBefore(li.nextElementSibling, li);
        b.focus();
      });
      return b;
    };
    li.append(label, move("up", "↑"), move("down", "↓"));
    box.append(li);
  }
}

// ---------- Agents ----------
for (const btn of wizard.querySelectorAll("[data-copy]")) {
  btn.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(btn.previousElementSibling.textContent);
      btn.textContent = "Copied";
      setTimeout(() => { btn.textContent = "Copy"; }, 2000);
    } catch {
      setStatus("wz-agents-status", "Select the command and copy it by hand.", "error");
    }
  });
}
// Bluetooth step: "TASKY BT" (3.1+) turns it on and opens Controls; then
// watch for the pairing. Older firmware: Bluetooth starts when you open Controls.
let btPoll = null;
$("wz-bt-on").addEventListener("click", (e) => busy(e.currentTarget, async () => {
  let r;
  try {
    r = await link.send("TASKY BT");
  } catch {
    r = null;
  }
  if (!r?.ok) {
    setStatus("wz-bt-status", "On Tasky, open the Controls screen (MODE): Bluetooth turns on there. Then follow the steps below.", "ok");
    return;
  }
  setStatus("wz-bt-status", r.connected ? "Connected ✓" : "Bluetooth is on. Now connect to “Tasky” in your Bluetooth settings…", "ok");
  clearInterval(btPoll);
  const until = Date.now() + 180000;
  btPoll = setInterval(async () => {
    if (!link || Date.now() > until) return clearInterval(btPoll);
    try {
      const s = await link.send("TASKY BT?");
      if (s?.connected) {
        clearInterval(btPoll);
        setStatus("wz-bt-status", "Connected ✓ Hold the knob on Tasky to pick Mac, Windows or Phone.", "ok");
      }
    } catch {
      clearInterval(btPoll);
    }
  }, 2000);
}));

const testCard = (state) => "TASKY AGENT " + JSON.stringify({ id: "web-test", label: "Test", state, msg: "Hello from the setup page" });
let testCardTimer = null;
const removeTestCard = async () => {
  clearTimeout(testCardTimer);
  testCardTimer = null;
  await link?.send(testCard("end")).catch(() => {});
};
$("wz-agent-test").addEventListener("click", (e) => busy(e.currentTarget, async () => {
  try {
    await link.send(testCard("waiting"));
    setStatus("wz-agents-status", "Look at Tasky: a \"Test\" card on the Agents screen. It goes away in 10 seconds.", "ok");
    clearTimeout(testCardTimer);
    testCardTimer = setTimeout(removeTestCard, 10000);
  } catch {
    setStatus("wz-agents-status", "Tasky didn't answer. Flash the latest firmware above, then reconnect.", "error");
  }
}));
$("wz-disconnect").addEventListener("click", (e) => busy(e.currentTarget, async () => {
  if (testCardTimer) await removeTestCard();  // don't leave the test card behind
  await link?.close();
  link = null;
  $("wz-disconnect").textContent = "Disconnected. Claude Code can use Tasky now.";  // e.currentTarget is null after an await
}));

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
    const cal = calLink();
    if (!cal) return null;
    if (/\/embed/i.test(cal) || (/calendar\.google\.com\/calendar\/u\//i.test(cal) && !/\/ical\//i.test(cal))) {
      throw new Error("That's the web page, not the iCal link — copy the Secret address in iCal format.");
    }
    if (!/^https?:\/\/[^/]+\.[^/]+/i.test(cal)) throw new Error("That doesn't look like an iCal link. It should start with https:// or webcal://");
    return { cal };
  },
  tasks: () => ({ tasks: [0, 1, 2].map((i) => $("wz-t" + i).value.trim()) }),
  github: async () => {
    const ghu = ghName();
    if (ghu && (await lookupGitHub()) === "missing") throw new Error("No GitHub user with that name");
    return { ghu };
  },
  ai: () => {
    const key = $("wz-llmkey").value.trim();
    const llm = Number(wizard.querySelector('input[name="wz-llm"]:checked').value);
    return key ? { llm, llmKey: key } : { llm };
  },
  screens: () => ({
    screens: [...$("wz-screens").children].filter((li) => li.querySelector("input").checked).map((li) => li.dataset.id),
  }),
  phrases: () => {
    // The screen font is plain ASCII: "Mañana sí" becomes "Manana si"
    const ascii = (p) => p.normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/[^\x20-\x7e]/g, "").replace(/ {2,}/g, " ").trim();
    const list = $("wz-phrases").value.split("\n").map(ascii).filter(Boolean);
    if (list.length > 12) throw new Error(`Up to 12 phrases (you have ${list.length}).`);
    const long = list.find((p) => p.length > 60);
    if (long) throw new Error(`"${long.slice(0, 24)}…" is longer than 60 characters.`);
    return { phrases: list };  // empty = back to the built-in phrases
  },
  light: () => ({ light: Number(wizard.querySelector('input[name="wz-light"]:checked').value) }),
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
      const payload = await SAVERS[step]();
      if (payload) {
        const r = await link.set(payload);
        if (!r.ok) throw new Error(r.error || "Tasky didn't accept that");
      }
      if (payload && AFTER_SAVE[step]?.()) return;  // the step stays open (e.g. to test the calendar)
      next(step);
    } catch (err) {
      if (statusEl) { statusEl.textContent = err.message; statusEl.className = "wz-status error"; }
      else alert(err.message);
    }
  }));
}

// ---------- Calendar ----------
// Pasted links often carry spaces or line breaks; webcal:// is https:// for Tasky.
const calLink = () => $("wz-cal").value.replace(/\s+/g, "").replace(/^webcal:\/\//i, "https://");

for (const radio of wizard.querySelectorAll('input[name="wz-calprov"]')) {
  radio.addEventListener("change", () => {
    for (const ol of wizard.querySelectorAll(".wz-guide")) ol.hidden = ol.dataset.prov !== radio.value;
  });
}

const AFTER_SAVE = {
  // Firmware 2.13+ can fetch the calendar right away: offer a test before moving on.
  calendar: () => {
    if (older(state.v, [2, 13, 0])) return false;
    setStatus("wz-cal-status", "Saved ✓ Test it, or continue.", "ok");
    $("wz-caltest").hidden = false;
    wizard.querySelector('.wz-step[data-step="calendar"] [data-skip]').textContent = "Next";
    return true;
  },
};

$("wz-caltest").addEventListener("click", (e) => busy(e.currentTarget, async () => {
  setStatus("wz-cal-status", "Tasky is downloading your calendar…");
  try {
    const r = await link.send("TASKY CALTEST", 75000);  // a big calendar takes a while
    if (!r.ok) { setStatus("wz-cal-status", r.error || "Tasky couldn't read that calendar.", "error"); return; }
    const plural = (n, word) => `${n} ${word}${n === 1 ? "" : "s"}`;
    const nextUp = r.next ? ` Next: ${r.next}${r.nextTime ? ` at ${r.nextTime}` : ""}` : "";
    setStatus("wz-cal-status", `Found ${plural(r.events ?? 0, "event")} this month (${r.today ?? 0} today).${nextUp} ✓`, "ok");
  } catch {
    setStatus("wz-cal-status", "Tasky didn't answer in 30 seconds. Check it's on Wi-Fi, then try again.", "error");
  }
}));

// ---------- GitHub ----------
// Preview only: a profile card and the last 12 weeks of public events. API data
// goes into the page with textContent / setAttribute, never as HTML.
const ghName = () => $("wz-ghu").value.trim().replace(/^@/, "");
const ghCache = new Map();  // name -> Promise<user | null>; null = no such user (60 API calls/hour without a token)
const GH_GREENS = ["#161b22", "#0e4429", "#006d32", "#26a641", "#39d353"];

async function ghFetch(path) {
  const r = await fetch("https://api.github.com/" + path, { headers: { Accept: "application/vnd.github+json" } });
  if (r.status === 404) return null;
  if (!r.ok) throw new Error(`GitHub ${r.status}`);
  return r.json();
}

// Resolves to "ok", "missing" or "unknown" (offline, rate-limited: never blocks saving).
async function lookupGitHub() {
  const name = ghName();
  if (!/^[a-z\d][a-z\d-]{0,38}$/i.test(name)) {  // GitHub's username rules
    $("wz-gh-user").hidden = $("wz-gh-activity").hidden = true;
    if (!name) return "unknown";
    setStatus("wz-github-status", "No GitHub user with that name", "error");
    return "missing";
  }
  if (!ghCache.has(name)) ghCache.set(name, ghFetch("users/" + encodeURIComponent(name)));
  let user;
  try { user = await ghCache.get(name); } catch { ghCache.delete(name); return "unknown"; }
  if (name !== ghName()) return user ? "ok" : "missing";  // the user kept typing: don't render a stale answer
  $("wz-gh-user").hidden = !user;
  $("wz-gh-activity").hidden = true;
  if (!user) {
    setStatus("wz-github-status", "No GitHub user with that name", "error");
    return "missing";
  }
  setStatus("wz-github-status", "");
  const avatar = $("wz-gh-avatar");
  if (/^https:\/\//.test(user.avatar_url || "")) avatar.setAttribute("src", user.avatar_url);
  else avatar.removeAttribute("src");
  $("wz-gh-name").textContent = user.name || user.login;
  $("wz-gh-login").textContent = "@" + user.login;
  showActivity(user.login);
  return "ok";
}

async function showActivity(login) {
  let events;
  try { events = await ghFetch(`users/${encodeURIComponent(login)}/events/public?per_page=100`); } catch { return; }
  if (!Array.isArray(events) || login.toLowerCase() !== ghName().toLowerCase()) return;
  const dayKey = (d) => `${d.getFullYear()}-${d.getMonth() + 1}-${d.getDate()}`;
  const counts = new Map();
  for (const ev of events) {
    const n = ev.type === "PushEvent" ? Number(ev.payload?.size ?? ev.payload?.commits?.length ?? 1) || 1 : 1;
    const k = dayKey(new Date(ev.created_at));
    counts.set(k, (counts.get(k) || 0) + n);
  }
  // 12 columns (weeks, oldest first) x 7 rows (Sunday on top), ending today.
  const today = new Date();
  const days = [];
  for (const d = new Date(today.getFullYear(), today.getMonth(), today.getDate() - today.getDay() - 77); d <= today; d.setDate(d.getDate() + 1)) {
    days.push([d.toLocaleDateString(), counts.get(dayKey(d)) || 0]);
  }
  const max = Math.max(1, ...days.map(([, c]) => c));
  const grid = $("wz-gh-grid");
  grid.textContent = "";
  let total = 0;
  for (const [date, c] of days) {
    total += c;
    const cell = document.createElement("span");
    cell.style.background = GH_GREENS[c ? Math.ceil((c / max) * 4) : 0];
    cell.setAttribute("title", `${c} on ${date}`);
    grid.append(cell);
  }
  grid.setAttribute("aria-label", `${total} public contributions in the last 12 weeks`);
  $("wz-gh-activity").hidden = false;
}

let ghTimer = null;
$("wz-ghu").addEventListener("input", () => {
  clearTimeout(ghTimer);
  setStatus("wz-github-status", "");
  if (!ghName()) { $("wz-gh-user").hidden = $("wz-gh-activity").hidden = true; return; }
  ghTimer = setTimeout(lookupGitHub, 500);
});

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

// ---------- After flashing: on to setup ----------
// ESP Web Tools adds an <ewt-install-dialog> to <body> and fires "closed" on it.
// Its install state is internal: if we can't read it, assume the flash went well.
new MutationObserver((records) => {
  for (const r of records) for (const el of r.addedNodes) {
    if (el.localName !== "ewt-install-dialog") continue;
    el.addEventListener("closed", () => {
      const knows = "_installState" in el;
      if ((knows && el._installState?.state !== "finished") || link || !("serial" in navigator)) return;
      setTimeout(() => {  // the dialog restores page scrolling right after "closed"
        $("setup").scrollIntoView({ behavior: "smooth", block: "start" });
        $("wz-connect").focus({ preventScroll: true });
        setStatus("wz-connect-status", "Now set it up: click Connect Tasky", "ok");
      }, 50);
    }, { once: true });
  }
}).observe(document.body, { childList: true });

show("connect");
