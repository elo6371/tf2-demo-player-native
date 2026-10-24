import fs from 'node:fs';

function cstr(d, p) { const e = d.indexOf(0, p); return [d.toString('latin1', p, e), e + 1]; }

class VPK {
  constructor(path) {
    this.path = path;
    const d = fs.readFileSync(path);
    this.d = d;
    const sig = d.readUInt32LE(0), ver = d.readUInt32LE(4), tree = d.readUInt32LE(8);
    if (sig !== 0x55AA1234) throw new Error('bad sig');
    this.version = ver;
    let off = 12;
    if (ver === 2) { this.fileDataSectionSize = d.readUInt32LE(12); off = 28; }
    this.treeStart = off; this.treeSize = tree; this.dataStart = off + tree;
    this.entries = new Map();
    let p = off;
    let [ext, p2] = cstr(d, p); p = p2;
    while (ext !== '') {
      let [pathS, p3] = cstr(d, p); p = p3;
      while (pathS !== '') {
        let [fn, p4] = cstr(d, p); p = p4;
        while (fn !== '') {
          const crc = d.readUInt32LE(p); const preload = d.readUInt16LE(p + 4);
          const arch = d.readUInt16LE(p + 6); const eoff = d.readUInt32LE(p + 8);
          const elen = d.readUInt32LE(p + 12); const term = d.readUInt16LE(p + 16);
          p += 18;
          if (term !== 0xFFFF) throw new Error('bad term ' + term + ' at ' + p);
          const pre = d.subarray(p, p + preload); p += preload;
          const full = (pathS ? pathS + '/' : '') + fn + '.' + ext;
          this.entries.set(full.toLowerCase(), { arch, eoff, elen, pre });
          [fn, p] = cstr(d, p);
        }
        [pathS, p] = cstr(d, p);
      }
      [ext, p] = cstr(d, p);
    }
  }
  read(name) {
    const e = this.entries.get(name.toLowerCase());
    if (!e) return null;
    const parts = [e.pre];
    if (e.elen) {
      if (e.arch === 0x7FFF) { parts.push(this.d.subarray(this.dataStart + e.eoff, this.dataStart + e.eoff + e.elen)); }
      else {
        const base = this.path.split(/[\\/]/).pop();
        const vol = this.path.replace(/_dir\.vpk$/, '_' + String(e.arch).padStart(3, '0') + '.vpk');
        const vd = fs.readFileSync(vol);
        parts.push(vd.subarray(e.eoff, e.eoff + e.elen));
      }
    }
    return Buffer.concat(parts);
  }
}

const path = process.argv[2];
const pattern = new RegExp(process.argv[3] ?? '.', 'i');
const v = new VPK(path);
const hits = [...v.entries.keys()].filter(k => pattern.test(k));
console.log(JSON.stringify({ vpk: path.split('/').pop(), entries: v.entries.size, hits: hits.slice(0, 200), hitCount: hits.length }, null, 2));
