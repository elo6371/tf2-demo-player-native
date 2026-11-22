
import fs from "node:fs";
const cam = new Map();
for (const line of fs.readFileSync(new URL("./inputs/newpov-cam.out", import.meta.url), "utf8").split(/\r?\n/)) {
  const m = line.match(/^camera at tick=(\d+) found=(\d+) sample_tick=(\d+) .* origin=([-\d.]+),([-\d.]+),([-\d.]+)/);
  if (m) cam.set(Number(m[1]), { st: +m[3], p: [+m[4], +m[5], +m[6]] });
}
const camTicks = [...cam.keys()].sort((a, b) => a - b);
function camNearest(dt) { let lo = 0, hi = camTicks.length - 1; while (hi - lo > 1) { const mid = (lo + hi) >> 1; if (camTicks[mid] < dt) lo = mid; else hi = mid; } const a = camTicks[lo], b = camTicks[hi]; return Math.abs(a - dt) <= Math.abs(b - dt) ? cam.get(a) : cam.get(b); }
const byTick = new Map();
let cur = null;
for (const line of fs.readFileSync(new URL("./inputs/newpov-sweep2.out", import.meta.url), "utf8").split(/\r?\n/)) {
  const h = line.match(/^observer-focus at tick=(\d+) status=(\S+)/);
  if (h) { cur = { tick: +h[1], status: h[2], rows: [] }; byTick.set(cur.tick, cur); continue; }
  const r = line.match(/^  observer entity=(\d+) .* mode=(\d+) targetHandle=(\d+) targetIndex=(\d+) targetSerial=(\d+) targetPresent=(\d+) follows=(\d+)/);
  if (!r || !cur) continue;
  const o = line.match(/selfOrigin=([-\d.]+),([-\d.]+),([-\d.]+)/);
  const tg = line.match(/targetOrigin=([-\d.]+),([-\d.]+),([-\d.]+)/);
  cur.rows.push({ entity: +r[1], mode: +r[2], idx: +r[4], present: +r[6], follows: +r[7], so: o ? [+o[1], +o[2], +o[3]] : null, to: tg ? [+tg[1], +tg[2], +tg[3]] : null });
}
const ticks = [...byTick.values()];
const allRows = ticks.flatMap(t => t.rows);
console.log("ticks=" + ticks.length + " allRows=" + allRows.length + " follows1=" + allRows.filter(r => r.follows === 1).length + " withTargetOrigin=" + allRows.filter(r => r.to).length + " withSelfOrigin=" + allRows.filter(r => r.so).length);
const d3 = (a, b) => Math.hypot(a[0]-b[0], a[1]-b[1], a[2]-b[2]);
// off-scan: for each sampled tick, min distance from camera to ANY row's selfOrigin / targetOrigin
function scan(kind) {
  const out = [];
  for (let off = 55000; off <= 62000; off += 100) {
    const ds = [];
    for (const t of ticks) {
      const cand = t.rows.filter(r => r[kind]);
      if (!cand.length) continue;
      const c = camNearest(t.tick - off);
      if (!c) continue;
      let best = 1e9;
      for (const r of cand) best = Math.min(best, d3(c.p, r[kind]));
      ds.push(best);
    }
    ds.sort((a, b) => a - b);
    out.push({ off, n: ds.length, med: ds[Math.floor(ds.length/2)], p25: ds[Math.floor(ds.length*0.25)], frac50: ds.filter(d => d < 50).length / ds.length, frac100: ds.filter(d => d < 100).length / ds.length });
  }
  out.sort((a, b) => a.med - b.med);
  return out.slice(0, 3).map(o => ({ ...o, med: +o.med.toFixed(1), p25: +o.p25.toFixed(1) }));
}
console.log("TARGET", JSON.stringify(scan("to")));
console.log("SELF", JSON.stringify(scan("so")));
