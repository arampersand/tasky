// Board selector + latest-release info for the flasher page.
const installer = document.getElementById("installer");

for (const radio of document.querySelectorAll('input[name="board"]')) {
  radio.addEventListener("change", () => installer.setAttribute("manifest", `/firmware/manifest-${radio.value}.json`));
}

let latest = { version: null };
try {
  const res = await fetch("/firmware/latest.json", { cache: "no-store" });
  if (res.ok) latest = await res.json();
} catch { /* offline or not built yet: fall through to "no release" */ }

const $ = (id) => document.getElementById(id);
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
}
