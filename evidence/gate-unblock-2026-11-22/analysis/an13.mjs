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
const ents = [...new Set(rows.map(r=>r.ent))].sort((a,b)=>a-b);
const players = ents.filter(e=>e>=2 && e<=13);
console.log("entities in table", ents.length, "range", ents[0]+".."+ents[ents.length-1]);
console.log("player-shaped entities (2..13):", players.length, JSON.stringify(players));
const byQ = new Map();
for (const r of rows) { if (!byQ.has(r.q)) byQ.set(r.q, []); byQ.get(r.q).push(r); }
const ticks = [...byQ.keys()].sort((a,b)=>a-b);
const histNZ = {};
let ticksWithNZ = 0; const nzPerTick = [];
for (const q of ticks) {
  const nz = byQ.get(q).filter(r=>r.mode!==0).length;
  nzPerTick.push(nz);
  histNZ[nz]=(histNZ[nz]||0)+1;
  if (nz>0) ticksWithNZ++;
}
console.log("ticks", ticks.length, "ticks with >=1 mode!=0", ticksWithNZ, "of", ticks.length, "=", (ticksWithNZ/ticks.length*100).toFixed(1)+"%");
const sortedNZ = [...nzPerTick].sort((a,b)=>a-b);
console.log("mode!=0 rows per tick: min", sortedNZ[0], "median", sortedNZ[Math.floor(sortedNZ.length/2)], "max", sortedNZ[sortedNZ.length-1]);
console.log("mode!=0 per-tick histogram", JSON.stringify(histNZ));
const ineye = rows.filter(r=>r.mode===4 && r.to && r.so);
const eq = ineye.filter(r=>r.to[0]===r.so[0] && r.to[1]===r.so[1] && r.to[2]===r.so[2]).length;
console.log("in-eye rows with both origins", ineye.length, "targetOrigin===selfOrigin", eq, "=", (eq/ineye.length*100).toFixed(1)+"%");
const sample = ineye.find(r=>r.res===145589);
if (sample) {
  const c = cams.filter(x=>Math.abs(x.t-(145589-59567))<=200).sort((a,b)=>Math.abs(a.t-(145589-59567))-Math.abs(b.t-(145589-59567)))[0]
        || cams.filter(x=>Math.abs(x.t-86000)<=200)[0];
  const d = sample.to.map((v,i)=>v-[c.x,c.y,c.z][i]);
  console.log("sample res="+sample.res+" demo="+(145589-59567)+" target="+sample.to.join(",")+" nearest cam tick="+c.t+" origin="+c.x+","+c.y+","+c.z);
  console.log("sample delta="+d.map(v=>v.toFixed(6)).join(" ")+" |per-axis|="+d.map(v=>Math.abs(v).toFixed(6)).join(" ")+" 3D="+Math.hypot(...d).toFixed(4));
  const allTgt = ineye.filter(r=>r.res===sample.res);
  console.log("in-eye rows sharing that resolved tick:", allTgt.length);
}
