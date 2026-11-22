
import fs from "node:fs";
const sweep = fs.readFileSync(new URL("./inputs/newpov-sweep2.out", import.meta.url), "utf8").split(/\r?\n/);
const cam = fs.readFileSync(new URL("./inputs/newpov-cam.out", import.meta.url), "utf8").split(/\r?\n/);
const cams = [];
for (const l of cam) {
  const m = l.match(/camera at tick=(\d+) found=1 sample_tick=(\d+) source=cmdinfo origin=([-\d.]+),([-\d.]+),([-\d.]+)/);
  if (m) cams.push({ q: +m[1], t: +m[2], x:+m[3], y:+m[4], z:+m[5] });
}
const rows = [];
let cur = 0, status="";
for (const l of sweep) {
  const h = l.match(/observer-focus at tick=(\d+) status=(\S+) resolved=(\d+)/);
  if (h) { cur = +h[1]; status = h[2]; continue; }
  const r = l.match(/observer entity=(\d+) class=(\d+) (\S+) mode=(\d+) targetHandle=(-?\d+) targetIndex=(-?\d+) targetSerial=(\d+) targetPresent=(\d) follows=(\d)/);
  if (r) {
    const o = l.match(/targetOrigin=([-\d.]+),([-\d.]+),([-\d.]+) selfOrigin=([-\d.]+),([-\d.]+),([-\d.]+)/);
    rows.push({ q: cur, res: null, status, ent:+r[1], mode:+r[4], ti:+r[6], present:+r[8], follows:+r[9],
      to: o ? [+o[1],+o[2],+o[3]] : null, so: o ? [+o[4],+o[5],+o[6]] : null });
  }
}
// resolved tick per query from header line: use q as proxy (resolved is checkpoint, may differ)
const OFF = 59567;
const camByNear = (demoTick, win) => cams.filter(c => Math.abs(c.t - demoTick) <= win);
function stats(pairs){ if(!pairs.length) return "n=0"; const ds=pairs.map(p=>p.d).sort((a,b)=>a-b); const q=(p)=>ds[Math.min(ds.length-1,Math.floor(p*ds.length))];
  return "n="+ds.length+" med="+q(0.5).toFixed(1)+" p10="+q(0.1).toFixed(1)+" frac<=50="+(ds.filter(d=>d<=50).length/ds.length).toFixed(3)+" frac<=100="+(ds.filter(d=>d<=100).length/ds.length).toFixed(3); }
const ineye = rows.filter(r => r.mode===4 && r.to);
const anyrows = rows.filter(r => r.to);
console.log("rows", rows.length, "in-eye w/targetOrigin", ineye.length, "any w/targetOrigin", anyrows.length, "cams", cams.length);
for (const win of [200, 400, 800, 1600]) {
  const a = ineye.map(r => { const near = camByNear(r.q - OFF, win); if(!near.length) return null; const best = near.map(c=>({c,d:Math.hypot(c.x-r.to[0],c.y-r.to[1],c.z-r.to[2])})).sort((x,y)=>x.d-y.d)[0]; return {d:best.d};}).filter(Boolean);
  const b = ineye.map(r => { const near = camByNear(r.q - OFF, win); if(!near.length) return null; const best = near.map(c=>({c,d:Math.hypot(c.x-r.so[0],c.y-r.so[1],c.z-r.so[2])})).sort((x,y)=>x.d-y.d)[0]; return {d:best.d};}).filter(Boolean);
  console.log("win="+win+" targetOrigin-vs-cam:", stats(a), "| selfOrigin-vs-cam:", stats(b));
}
// constant-offset scan around the oracle value: which offset maximises targetOrigin agreement?
const scan = [];
for (let off = OFF-1500; off <= OFF+1500; off += 100) {
  const a = ineye.map(r => { const near = camByNear(r.q - off, 200); if(!near.length) return null; const best = near.map(c=>({c,d:Math.hypot(c.x-r.to[0],c.y-r.to[1],c.z-r.to[2])})).sort((x,y)=>x.d-y.d)[0]; return {d:best.d};}).filter(Boolean);
  if (a.length < 100) continue;
  const ds = a.map(x=>x.d).sort((x,y)=>x-y);
  scan.push({off, n:a.length, med: ds[Math.floor(ds.length/2)], frac50: ds.filter(d=>d<=50).length/ds.length});
}
scan.sort((x,y)=>y.frac50-x.frac50);
console.log("top offsets:", JSON.stringify(scan.slice(0,6).map(s=>({off:s.off,n:s.n,med:+s.med.toFixed(1),f50:+s.frac50.toFixed(3)}))));
