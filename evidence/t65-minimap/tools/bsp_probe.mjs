import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const bspPath = process.argv[2];
const outDir = process.argv[3];
const buf = fs.readFileSync(bspPath);
const ident = buf.toString('latin1', 0, 4);
const version = buf.readInt32LE(4);
const lumps = [];
for (let i = 0; i < 64; i++) {
  const off = 8 + i * 16;
  lumps.push({ index: i, fileofs: buf.readInt32LE(off), filelen: buf.readInt32LE(off + 4), version: buf.readInt32LE(off + 8), fourCC: buf.readInt32LE(off + 12) });
}
const LUMP = { ENTITIES: 0, PLANES: 1, VERTEXES: 3, NODES: 5, TEXINFO: 6, FACES: 7, LEAFS: 10, MODELS: 14, DISPINFO: 33, PAKFILE: 40 };
const readLump = (i) => { const l = lumps[i]; return buf.subarray(l.fileofs, l.fileofs + l.filelen); };

// --- entities ---
const entRaw = readLump(LUMP.ENTITIES).toString('utf8');
const entities = [];
let cur = null;
const lines = entRaw.split(/\r?\n/);
for (const line of lines) {
  const t = line.trim();
  if (t === '{') { cur = {}; continue; }
  if (t === '}') { if (cur) entities.push(cur); cur = null; continue; }
  if (!cur) continue;
  const m = t.match(/^"([^"]*)"\s+"([^"]*)"/);
  if (m) cur[m[1]] = m[2];
}
const byClass = {};
for (const e of entities) { const c = e.classname || '(none)'; byClass[c] = (byClass[c] || 0) + 1; }
const originOf = (e) => { const p = (e.origin || '').trim().split(/\s+/).map(Number); return p.length === 3 && p.every(Number.isFinite) ? p : null; };
const interesting = ['info_player_teamspawn','team_control_point','team_control_point_master','team_train_watcher','path_track','func_capturezone','item_teamflag','info_player_start','info_player_observer','func_respawnroom','trigger_multiple','ambient_generic'];
const entsOut = {};
for (const c of interesting) {
  const list = entities.filter(e => e.classname === c).map(e => ({ origin: originOf(e), targetname: e.targetname ?? null, team: e.team ?? e.TeamNum ?? null, target: e.target ?? null, model: e.model ?? null, angles: e.angles ?? null, raw: Object.keys(e).length }));
  if (list.length) entsOut[c] = list;
}

// --- models ---
const modelLump = readLump(LUMP.MODELS);
const models = [];
for (let o = 0; o + 48 <= modelLump.length; o += 48) {
  models.push({
    mins: [modelLump.readFloatLE(o), modelLump.readFloatLE(o+4), modelLump.readFloatLE(o+8)],
    maxs: [modelLump.readFloatLE(o+12), modelLump.readFloatLE(o+16), modelLump.readFloatLE(o+20)],
    origin: [modelLump.readFloatLE(o+24), modelLump.readFloatLE(o+28), modelLump.readFloatLE(o+32)],
    headnode: modelLump.readInt32LE(o+36), firstface: modelLump.readInt32LE(o+40), numfaces: modelLump.readInt32LE(o+44),
  });
}

// --- vertices ---
const vertLump = readLump(LUMP.VERTEXES);
const verts = [];
for (let o = 0; o + 12 <= vertLump.length; o += 12) verts.push([vertLump.readFloatLE(o), vertLump.readFloatLE(o+4), vertLump.readFloatLE(o+8)]);
let vmin = [Infinity,Infinity,Infinity], vmax = [-Infinity,-Infinity,-Infinity];
for (const v of verts) for (let k=0;k<3;k++){ if(v[k]<vmin[k])vmin[k]=v[k]; if(v[k]>vmax[k])vmax[k]=v[k]; }

// --- faces (version 20: 56 bytes) ---
const faceLump = readLump(LUMP.FACES);
const faceSize = 56;
const faces = [];
for (let o = 0; o + faceSize <= faceLump.length; o += faceSize) {
  faces.push({ planenum: faceLump.readUInt16LE(o), side: faceLump.readUInt8(o+2), onNode: faceLump.readInt32LE(o+4), firstedge: faceLump.readInt32LE(o+8), numedges: faceLump.readInt16LE(o+12), texinfo: faceLump.readInt16LE(o+14), dispinfo: faceLump.readInt16LE(o+16), surfaceFogVolumeID: faceLump.readInt16LE(o+18) });
}

// --- pakfile zip listing ---
const pak = readLump(LUMP.PAKFILE);
let pakEntries = [];
try {
  // find end of central directory
  let eocd = -1;
  for (let i = pak.length - 22; i >= 0; i--) { if (pak.readUInt32LE(i) === 0x06054b50) { eocd = i; break; } }
  if (eocd >= 0) {
    const count = pak.readUInt16LE(eocd + 10);
    let cdOff = pak.readUInt32LE(eocd + 16);
    for (let n = 0; n < count; n++) {
      if (pak.readUInt32LE(cdOff) !== 0x02014b50) break;
      const nameLen = pak.readUInt16LE(cdOff + 28);
      const extraLen = pak.readUInt16LE(cdOff + 30);
      const commentLen = pak.readUInt16LE(cdOff + 32);
      const name = pak.toString('utf8', cdOff + 46, cdOff + 46 + nameLen);
      pakEntries.push({ name, compressedSize: pak.readUInt32LE(cdOff + 20), size: pak.readUInt32LE(cdOff + 24) });
      cdOff += 46 + nameLen + extraLen + commentLen;
    }
  }
} catch (e) { pakEntries = [{ name: 'ERROR ' + e.message, compressedSize: 0, size: 0 }]; }

const out = {
  bspPath, bytes: buf.length, sha256: crypto.createHash('sha256').update(buf).digest('hex'),
  ident, version,
  lumps: lumps.filter(l => l.filelen > 0).map(l => ({ index: l.index, fileofs: l.fileofs, filelen: l.filelen, version: l.version })),
  entityCount: entities.length,
  classCounts: Object.fromEntries(Object.entries(byClass).sort((a,b)=>b[1]-a[1]).slice(0, 40)),
  entities: entsOut,
  worldModel: models[0] ?? null,
  modelCount: models.length,
  vertexCount: verts.length,
  vertexMin: vmin, vertexMax: vmax,
  faceCount: faces.length,
  pakEntryCount: pakEntries.length,
  pakOverviewEntries: pakEntries.filter(e => /overview|radar|minimap/i.test(e.name)),
  pakSampleEntries: pakEntries.slice(0, 10).map(e => e.name),
};
fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(path.join(outDir, path.basename(bspPath) + '.probe.json'), JSON.stringify(out, null, 2));
console.log(JSON.stringify({ ident, version, entityCount: entities.length, worldModel: out.worldModel, vertexCount: verts.length, vertexMin: vmin, vertexMax: vmax, faceCount: faces.length, pakEntryCount: pakEntries.length, pakOverview: out.pakOverviewEntries, classes: out.classCounts, ents: Object.fromEntries(Object.entries(entsOut).map(([k,v])=>[k, v.length])) }, null, 2));
