// Downloads the latest GitHub Release into public/firmware/ and writes the
// ESP Web Tools manifests. Runs at Vercel build time so the .bin files are
// served same-origin (GitHub release assets send no CORS headers, so the
// browser cannot fetch them directly).
//
// The manifest flashes the individual parts (bootloader, partitions, boot_app0,
// app) listed in tasky-<env>-parts.json instead of the merged image: the merged
// image fills the NVS gap with 0xFF and would wipe the user's settings on a
// reinstall. ESP Web Tools still offers an explicit "erase device" option.
//
// Env: TASKY_REPO (default arampersand/tasky), GITHUB_TOKEN (optional, rate limit).
import { mkdir, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const REPO = process.env.TASKY_REPO ?? "arampersand/tasky";
const OUT = join(dirname(fileURLToPath(import.meta.url)), "..", "public", "firmware");
const BOARDS = [
  { id: "c3", env: "xiao_c3", chipFamily: "ESP32-C3" },
  { id: "c6", env: "xiao_c6", chipFamily: "ESP32-C6" },
];
const headers = { "User-Agent": "tasky-flash-build", Accept: "application/vnd.github+json" };
if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;

await mkdir(OUT, { recursive: true });
const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, { headers });
if (res.status === 404) {
  console.warn(`No release found for ${REPO} — the page will show "no release yet".`);
  await writeFile(join(OUT, "latest.json"), JSON.stringify({ version: null }));
  process.exit(0);
}
if (!res.ok) throw new Error(`GitHub API ${res.status}: ${await res.text()}`);
const rel = await res.json();
const version = rel.tag_name.replace(/^v/, "");

async function download(name) {
  const asset = rel.assets.find((a) => a.name === name);
  if (!asset) throw new Error(`Release ${rel.tag_name} has no asset ${name}`);
  const r = await fetch(asset.browser_download_url, { headers: { "User-Agent": headers["User-Agent"] } });
  if (!r.ok) throw new Error(`Download ${name}: ${r.status}`);
  return Buffer.from(await r.arrayBuffer());
}

for (const b of BOARDS) {
  const parts = JSON.parse((await download(`tasky-${b.env}-parts.json`)).toString("utf8"));
  for (const p of parts) await writeFile(join(OUT, p.path), await download(p.path));
  await writeFile(join(OUT, `manifest-${b.id}.json`), JSON.stringify({
    name: "Tasky",
    version,
    new_install_prompt_erase: true,
    builds: [{ chipFamily: b.chipFamily, parts }],
  }, null, 2));
  console.log(`✓ ${b.env}: ${parts.map((p) => `${p.path}@0x${p.offset.toString(16)}`).join(", ")}`);
}

await writeFile(join(OUT, "latest.json"), JSON.stringify({
  version,
  tag: rel.tag_name,
  notes: rel.body ?? "",
  publishedAt: rel.published_at,
  url: rel.html_url,
}, null, 2));
console.log(`Latest release: ${rel.tag_name}`);
