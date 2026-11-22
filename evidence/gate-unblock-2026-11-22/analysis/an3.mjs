import fs from "node:fs";
const IN = "./inputs/";
const rd = (n) => fs.readFileSync(new URL(IN + n, import.meta.url), "utf8");

const cam = rd("cam-rows.txt").trim().split("\n").map(l => { const p = l.trim().split(/\s+/); return { tick: +p[0], org: p[1] }; });
const runs = [];
let cur = 1;
for (let i = 1; i < cam.length; i++) {
  if (cam[i].org === cam[i-1].org) cur++; else { if (cur > 1) runs.push({ start: cam[i-cur].tick, end: cam[i-1].tick, n: cur, org: cam[i-1].org }); cur = 1; }
}
if (cur > 1) runs.push({ start: cam[cam.length-cur].tick, end: cam[cam.length-1].tick, n: cur, org: cam[cam.length-1].org });
console.log("camera samples:", cam.length, "distinct origins:", new Set(cam.map(c => c.org)).size, "held runs:", runs.length);
console.log(runs.slice(0, 12).map(r => `  ticks ${r.start}..${r.end} n=${r.n} org=${r.org}`).join("\n"));
// holds: identical x,y (ignore tiny z? no) — also count near-held (same x and y)
const rows = rd("sweep-rows.txt").trim().split("\n").map(l => l.trim().split(/\s+/).map(Number));
const byTick = new Map();
for (const [tick, ent, mode, fol, ti, tp] of rows) { if (!byTick.has(tick)) byTick.set(tick, []); byTick.get(tick).push({ ent, mode, fol, ti, tp }); }
console.log("ticks with rows:", byTick.size, "of", new Set(cam.map(c => c.tick)).size);
let ds = 0, dn = 0;
for (const [t, rs] of byTick) { if (rs.some(r => r.fol === 1)) ds++; else dn++; }
console.log("ticks with >=1 follows=1:", ds, "without:", dn);
// how many entities can be mode!=0 at the same time
const spread = {}; for (const [t, rs] of byTick) { spread[rs.length] = (spread[rs.length]||0)+1; }
console.log("rows-per-tick histogram:", JSON.stringify(spread));
