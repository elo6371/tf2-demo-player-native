
import fs from "node:fs";
const cams = [];
for (const l of fs.readFileSync(new URL("./inputs/newpov-cam.out", import.meta.url), "utf8").split(/\r?\n/)) {
  const m = l.match(/^camera at tick=(\d+) found=(\d+) sample_tick=(\d+) .* origin=([-\d.]+),([-\d.]+),([-\d.]+)/);
  if (m) cams.push({ q: +m[1], st: +m[3], p: [+m[4], +m[5], +m[6]] });
}
const rows = [];
let cur = -1;
for (const l of fs.readFileSync(new URL("./inputs/newpov-sweep2.out", import.meta.url), "utf8").split(/\r?\n/)) {
  const h = l.match(/^observer-focus at tick=(\d+)/);
  if (h) { cur = +h[1]; continue; }
  const r = l.match(/^  observer entity=(\d+) .* mode=(\d+) .*follows=(\d+)/);
  if (!r) continue;
  const so = l.match(/selfOrigin=([-\d.]+),([-\d.]+),([-\d.]+)/);
  if (!so) continue;
  rows.push({ tick: cur, e: +r[1], mode: +r[2], so: [+so[1], +so[2], +so[3]] });
}
const hits = new Map();
let total = 0;
for (const r of rows) {
  for (const c of cams) {
    const dx = c.p[0] - r.so[0], dy = c.p[1] - r.so[1], dz = c.p[2] - r.so[2];
    if (Math.abs(dx) < 3 && Math.abs(dz) < 3 && dy > 20 && dy < 90) {
      total++;
      const k = r.e;
      if (!hits.has(k)) hits.set(k, []);
      hits.get(k).push({ row: r.tick, cam: c.q, dx, dy, dz });
    }
  }
}
console.log("rows=" + rows.length + " cams=" + cams.length + " total matching pairs=" + total);
for (const [e, list] of [...hits.entries()].sort((a, b) => b[1].length - a[1].length)) {
  const dy = list.map(x => x.dy);
  console.log("entity " + e + " matches=" + list.length + " rowTicks=" + [...new Set(list.map(x => x.row))].slice(0, 8).join(",") + " camTicks=" + [...new Set(list.map(x => x.cam))].slice(0, 8).join(",") + " dy range " + Math.min(...dy).toFixed(1) + ".." + Math.max(...dy).toFixed(1));
}
