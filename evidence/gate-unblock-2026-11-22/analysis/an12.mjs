
import fs from "node:fs";
const sweep = fs.readFileSync(new URL("./inputs/newpov-sweep2.out", import.meta.url), "utf8").split(/\r?\n/);
const cam = fs.readFileSync(new URL("./inputs/newpov-cam.out", import.meta.url), "utf8").split(/\r?\n/);
const cams = [];
for (const l of cam) {
  const m = l.match(/camera at tick=(\d+) found=1 sample_tick=(\d+) source=cmdinfo origin=([-\d.]+),([-\d.]+),([-\d.]+)/);
  if (m) cams.push({ t: +m[2], x:+m[3], y:+m[4], z:+m[5] });
}
const rows = []; let curQ=0, curR=0, st="";
for (const l of sweep) {
  const h = l.match(/observer-focus at tick=(\d+) status=(\S+) resolved=(\d+)/);
  if (h) { curQ=+h[1]; curR=+h[3]; st=h[2]; continue; }
  const r = l.match(/observer entity=(\d+) class=(\d+) \S+ mode=(\d+) targetHandle=(-?\d+) targetIndex=(-?\d+) targetSerial=(\d+) targetPresent=(\d) follows=(\d)/);
  if (r) {
    const o = l.match(/targetOrigin=([-\d.]+),([-\d.]+),([-\d.]+) selfOrigin=([-\d.]+),([-\d.]+),([-\d.]+)/);
    rows.push({ q:curQ, res:curR, st, ent:+r[1], mode:+r[3], ti:+r[5], present:+r[7], follows:+r[8],
      to:o?[+o[1],+o[2],+o[3]]:null, so:o?[+o[4],+o[5],+o[6]]:null });
  }
}
const DIST = (a,b)=>Math.hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]);
function near(t, win){ return cams.filter(c=>Math.abs(c.t-t)<=win); }
console.log("rows",rows.length,"cams",cams.length,"checkpoint rows",rows.filter(r=>r.st==="checkpoint").length);
function run(label, list, offMode) {
  for (const win of [100,200,400]) {
    const ds = [];
    for (const r of list) {
      const anchor = (offMode==="res") ? r.res : r.q;
      const near_ = near(anchor - 59567, win);
      if (!near_.length) continue;
      const best = near_.map(c=>({d:DIST([c.x,c.y,c.z], r.to)})).sort((a,b)=>a.d-b.d)[0];
      ds.push(best.d);
    }
    ds.sort((a,b)=>a-b);
    const f = (k)=> ds.filter(d=>d<=k).length/ds.length;
    console.log(label+" anchor="+offMode+" win="+win+" n="+ds.length+" med="+ds[Math.floor(ds.length/2)].toFixed(1)+" f50="+f(50).toFixed(3)+" f100="+f(100).toFixed(3)+" f200="+f(200).toFixed(3));
  }
}
run("ineye-resolved", rows.filter(r=>r.mode===4&&r.to), "res");
run("ineye-query", rows.filter(r=>r.mode===4&&r.to), "q");
// mode distribution for entity 2 (the local-player table owner)
const e2 = rows.filter(r=>r.ent===2);
const byMode = {}; for (const r of e2) byMode[r.mode]=(byMode[r.mode]||0)+1;
console.log("entity2 rows", e2.length, "modes", JSON.stringify(byMode));
const e2f = {}; for (const r of e2) if (r.mode!==0) { e2f[r.mode]=(e2f[r.mode]||0)+1; }
console.log("entity2 nonzero", JSON.stringify(e2f));
const follows = e2.filter(r=>r.follows===1);
console.log("entity2 follows=1 rows", follows.length, follows.slice(0,4).map(r=>({q:r.q,res:r.res,mode:r.mode,ti:r.ti,to:r.to})));
