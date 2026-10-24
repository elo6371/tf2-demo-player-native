import fs from 'node:fs';
class VPK {
  constructor(path) {
    this.path = path; const d = fs.readFileSync(path); this.d = d;
    const sig = d.readUInt32LE(0), ver = d.readUInt32LE(4), tree = d.readUInt32LE(8);
    if (sig !== 0x55AA1234) throw new Error('bad sig');
    let off = 12; if (ver === 2) { off = 28; }
    this.treeStart = off; this.treeSize = tree; this.dataStart = off + tree;
    this.entries = new Map(); let p = off;
    const cstr = (p) => { const e = d.indexOf(0, p); return [d.toString('latin1', p, e), e + 1]; };
    let [ext, p2] = cstr(p); p = p2;
    while (ext !== '') {
      let [pathS, p3] = cstr(p); p = p3;
      while (pathS !== '') {
        let [fn, p4] = cstr(p); p = p4;
        while (fn !== '') {
          const preload = d.readUInt16LE(p + 4), arch = d.readUInt16LE(p + 6);
          const eoff = d.readUInt32LE(p + 8), elen = d.readUInt32LE(p + 12);
          p += 18; const pre = d.subarray(p, p + preload); p += preload;
          const full = (pathS ? pathS + '/' : '') + fn + '.' + ext;
          this.entries.set(full.toLowerCase(), { arch, eoff, elen, pre });
          [fn, p] = cstr(p);
        }
        [pathS, p] = cstr(p);
      }
      [ext, p] = cstr(p);
    }
  }
  read(name) {
    const e = this.entries.get(name.toLowerCase()); if (!e) return null;
    const parts = [e.pre];
    if (e.elen) {
      if (e.arch === 0x7FFF) parts.push(this.d.subarray(this.dataStart + e.eoff, this.dataStart + e.eoff + e.elen));
      else { const vol = this.path.replace(/_dir\.vpk$/, '_' + String(e.arch).padStart(3,'0') + '.vpk'); const vd = fs.readFileSync(vol); parts.push(vd.subarray(e.eoff, e.eoff + e.elen)); }
    }
    return Buffer.concat(parts);
  }
}
const v = new VPK(process.argv[2]);
for (const name of process.argv.slice(3)) {
  const b = v.read(name);
  if (!b) { console.log(name, 'MISSING'); continue; }
  const out = '/tmp/' + name.split('/').pop();
  fs.writeFileSync(out, b);
  console.log(name, 'bytes=' + b.length, '->', out);
  console.log('  hex:', b.subarray(0, 48).toString('hex').match(/.{1,8}/g).join(' '));
  console.log('  ascii:', JSON.stringify(b.subarray(0, 64).toString('latin1')));
}
