
import fs from "node:fs";
const cams = [];
for (const line of fs.readFileSync(new URL("./inputs/newpov-cam.out", import.meta.url), "utf8").split(/\r?\n/)) {
  const m = line.match(/^camera at tick=(\d+) found=(\d+) sample_tick=(\d+) .* origin=([-\d.]+),([-\d.]+),([-\d.]+)/);
  if (m) cams.push({ q: +m[1], st: +m[3], p: [+m[4], +m[5], +m[6]] });
}
const rows = [];
let cur = -1;
for (const line of fs.readFileSync(new URL("./inputs/newpov-sweep2.out", import.meta.url), "utf8").split(/\r?\n/)) {
  const h = line.match(/^observer-focus at tick=(\d+)/);
  if (h) { cur = +h[1]; continue; }
  const r = line.match(/^  observer entity=(\d+) .* mode=(\d+) .*follows=(\d+)/);
  if (!r) continue;
  const tg = line.match(/targetOrigin=([-\d.]+),([-\d.]+),([-\d.]+)/);
  if (!tg) continue;
  rows.push({ tick: cur, entity: +r[1], mode: +r[2], follows: +r[3], to: [+tg[1], +tg[2], +tg[3]] });
}
console.log("rows-with-targetOrigin=" + rows.length + " cams=" + cams.length);
let hits = [];
for (const r of rows) {
  for (const c of cams) {
    const dx = c.p[0] - r.to[0], dy = c.p[1] - r.to[1], dz = c.p[2] - r.to[2];
    if (Math.abs(dx) < 4 && Math.abs(dz) < 4 && dy > 15 && dy < 90) hits.push({ rowTick: r.tick, entity: r.entity, mode: r.mode, camTick: c.q, dx: +dx.toFixed(2), dy: +dy.toFixed(2), dz: +dz.toFixed(2) });
  }
}
console.log("geometric hits (eye-above-feet, any time)=" + hits.length);
const byRow = new Map();
for (const h of hits) { const k = h.rowTick + "/" + h.entity; byRow.set(k, (byRow.get(k) || 0) + 1); }
console.log("distinct rows matched=" + byRow.size + ", sample: " + JSON.stringify([...byRow.entries()].slice(0, 10)));
console.log("samples: " + JSON.stringify(hits.slice(0, 5)));
