import fs from "node:fs";
import { readdirSync } from "node:fs";
const rd = (n) => fs.readFileSync(new URL("./../probes/" + n, import.meta.url), "utf8");

// --- 1. the entity table of the substitute POV at demo tick 200000 (--props-at dump)
const all = rd("d2-props-all-200000.txt").split(/\r?\n/);
const ids = [];
for (const l of all) {
  const m = l.match(/entity=(\d+)/) || l.match(/^\s*EntityId\((\d+)\)/);
  if (m) ids.push(+m[1]);
}
const uniq = [...new Set(ids)].sort((a, b) => a - b);
console.log("props-at dump lines " + all.length);
console.log("entities " + uniq.length + " ids " + uniq.join(","));
console.log("has entity 18: " + uniq.includes(18));
const runs = [];
if (uniq.length) {
  let s = uniq[0], p = uniq[0];
  for (const v of uniq.slice(1)) {
    if (v === p + 1) { p = v; continue; }
    runs.push(s === p ? String(s) : s + ".." + p);
    s = p = v;
  }
  runs.push(s === p ? String(s) : s + ".." + p);
}
console.log("compact ranges: " + runs.join(" "));

// --- 2. which entity carries the local-player-exclusive block at server tick 199999
const orc = rd("d4-oracle-199999.txt").split(/\r?\n/);
let cur = null;
const owners = new Map();
for (const l of orc) {
  const e = l.match(/^\s*entity=EntityId\((\d+)\)/);
  if (e) { cur = +e[1]; continue; }
  if (/DT_LocalPlayerExclusive|DT_TFLocalPlayerExclusive/.test(l) && cur !== null) {
    owners.set(cur, (owners.get(cur) || 0) + 1);
  }
}
console.log("local-player-exclusive owners (entity -> prop lines): " + JSON.stringify([...owners.entries()]));
const tb = orc.filter((l) => /m_nTickBase/.test(l));
console.log("m_nTickBase line: " + (tb.length ? tb[0].trim() : "(none)"));
const pkt = orc.filter((l) => /^packet_entities/.test(l));
console.log("packet line: " + (pkt.length ? pkt[0].trim() : "(none)"));

// --- 3. absence of observer field names anywhere in the committed probes
const dir = new URL("./../probes/", import.meta.url);
for (const f of readdirSync(dir).sort()) {
  const t = fs.readFileSync(new URL(f, dir), "utf8");
  const hit = ["m_iObserverMode", "m_hObserverTarget", "24587"].filter((k) => t.includes(k));
  console.log("probe " + f + " observer-field hits: " + (hit.length ? hit.join(",") : "none"));
}
