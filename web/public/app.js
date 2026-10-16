// Edition + board selector and latest-release info for the flasher page.
const installer = document.getElementById("installer");
const picked = (name) => document.querySelector(`input[name="${name}"]:checked`).value;
const updateManifest = () =>
  installer.setAttribute("manifest", `/firmware/manifest-${picked("board")}${picked("edition")}.json`);
for (const radio of document.querySelectorAll('input[name="board"], input[name="edition"]')) {
  radio.addEventListener("change", updateManifest);
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
  // Releases before 2.7 have no Agents edition
  if (latest.available && !latest.available.includes("c3-agents")) {
    const agents = $("edition-agents");
    agents.querySelector("input").disabled = true;
    agents.querySelector("small").textContent = "Coming in the next release";
  }
}
