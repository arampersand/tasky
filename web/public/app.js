// Board selector, USB port filter and latest-release info for the flasher page.
const $ = (id) => document.getElementById(id);
const installer = $("installer");

// ---------- USB port picker: list only Tasky ----------
// The XIAO C3/C6 show up as Espressif's built-in USB-Serial/JTAG (vendor 0x303A),
// so filtering on it hides Bluetooth, debug consoles and every other serial port.
// Patched here so both the installer and the setup wizard use it.
const ESPRESSIF = 0x303a;
let showAllPorts = false;
if ("serial" in navigator) {
  const requestPort = navigator.serial.requestPort.bind(navigator.serial);
  navigator.serial.requestPort = (options = {}) =>
    requestPort(showAllPorts || options.filters ? options : { ...options, filters: [{ usbVendorId: ESPRESSIF }] });
  $("show-all-ports").addEventListener("change", (e) => { showAllPorts = e.target.checked; });

  // Once you've connected a Tasky here, Chrome reports it when it's plugged in.
  const showUsbStatus = async () => {
    const ports = await navigator.serial.getPorts();
    $("usb-status").hidden = !ports.some((p) => p.getInfo().usbVendorId === ESPRESSIF);
  };
  navigator.serial.addEventListener("connect", showUsbStatus);
  navigator.serial.addEventListener("disconnect", showUsbStatus);
  showUsbStatus();
}

// One firmware since 3.0 (the 2.x "-agents" manifests are no longer offered).
const updateManifest = () => {
  const board = document.querySelector('input[name="board"]:checked').value;
  installer.setAttribute("manifest", `/firmware/manifest-${board}.json`);
};
for (const radio of document.querySelectorAll('input[name="board"]')) {
  radio.addEventListener("change", updateManifest);
}

let latest = { version: null };
try {
  const res = await fetch("/firmware/latest.json", { cache: "no-store" });
  if (res.ok) latest = await res.json();
} catch { /* offline or not built yet: fall through to "no release" */ }

if (!latest.version) {
  installer.hidden = true;
  $("no-release").hidden = false;
  $("rel-version").textContent = "No release yet";
  $("rel-notes").textContent = "The first firmware release hasn't been published.";
} else {
  const date = new Date(latest.publishedAt).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
  $("rel-version").textContent = `v${latest.version}`;
  $("rel-date").textContent = date;
  $("rel-notes").textContent = latest.notes || "No notes for this release.";  // plain text, never HTML
  $("rel-link").href = latest.url;
  $("hero-version").textContent = `v${latest.version} · ESP32-C3 / C6 · open source`;
  // A board missing from the release can't be picked (latest.json lists what was built)
  for (const radio of document.querySelectorAll('input[name="board"]')) {
    if (latest.available && !latest.available.includes(radio.value)) radio.disabled = true;
  }
}
