import fs from "node:fs";
const IN = "./inputs/";
const rd = (n) => fs.readFileSync(new URL(IN + n, import.meta.url), "utf8");

const cam = new Map();
for (const l of rd("cam-rows.txt").trim().split("\n")) {
  const p = l.trim().split(/\s+/); const o = p[1].split(",").map(Number);
  cam.set(+p[0], o);
}
let tick = 0; const rows = [];
for (const line of rd("newpov-sweep2.out").split("\n")) {
  const m = line.match(/^observer-focus at tick=(\d+)/); if (m) { tick = +m[1]; continue; }
  if (!line.startsWith("  observer entity=")) continue;
  const f = {}; for (const tok of line.trim().split(/\s+/)) { const i = tok.indexOf("="); if (i > 0) f[tok.slice(0, i)] = tok.slice(i + 1); }
  const so = f.selfOrigin ? f.selfOrigin.split(",").map(Number) : null;
  const to = f.targetOrigin ? f.targetOrigin.split(",").map(Number) : null;
  rows.push({ tick, e: +f.entity, mode: +f.mode, follows: +f.follows, tp: +f.targetPresent, so, to });
}
const d = (a, b) => Math.hypot(a[0]-b[0], a[1]-b[1], a[2]-b[2]);
// per entity: distance camera<->selfOrigin at ticks where both exist
const stats = new Map();
for (const r of rows) {
  const c = cam.get(r.tick); if (!c || !r.so) continue;
  if (!stats.has(r.e)) stats.set(r.e, { n: 0, near: 0, sum: 0, dmin: 1e9, dmax: 0 });
  const s = stats.get(r.e); const dist = d(c, r.so); s.n++; s.sum += dist; s.dmin = Math.min(s.dmin, dist); s.dmax = Math.max(s.dmax, dist);
  if (dist <= 80) s.near++;
}
console.log("entity: rows / camera-within-80u / mean dist / min / max");
for (const [e, s] of [...stats].sort((a,b)=>a[0]-b[0])) console.log(`  e${e}: n=${s.n} near=${s.near} mean=${(s.sum/s.n).toFixed(1)} min=${s.dmin.toFixed(1)} max=${s.dmax.toFixed(1)}`);
// also check camera vs targetOrigin where follows=1
let nf = 0, near = 0; const ds = [];
for (const r of rows) { if (!r.follows || !r.to) continue; const c = cam.get(r.tick); if (!c) continue; nf++; const dd = d(c, r.to); ds.push(dd); if (dd <= 80) near++; }
ds.sort((a,b)=>a-b);
console.log(`follows=1 rows with camera: ${nf}, within 80u of targetOrigin: ${near}, median dist=${ds.length?ds[Math.floor(ds.length/2)].toFixed(2):"-"}`);
