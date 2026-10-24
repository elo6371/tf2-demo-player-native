import fs from 'node:fs';
const vpk = process.argv[2];
const pattern = new RegExp(process.argv[3] ?? '.', 'i');
const buf = fs.readFileSync(vpk);
const sig = buf.readUInt32LE(0);
const version = buf.readUInt32LE(4);
if (sig !== 0x55AA1234) { console.error('bad sig', sig.toString(16)); process.exit(2); }
const treeSize = buf.readUInt32LE(8);
let o = 12;
if (version === 2) o = 28;
const end = o + treeSize;
const entries = [];
function cstr() { const s = buf.toString('utf8', o, buf.indexOf(0, o)); o += s.length + 1; return s; }
while (o < end) {
  const ext = cstr();
  if (!ext) break;
  const p = cstr();
  const fn = cstr();
  for (;;) {
    const crc = buf.readUInt32LE(o); o += 4;
    const preload = buf.readUInt16LE(o); o += 2;
    const archiveIndex = buf.readUInt16LE(o); o += 2;
    const entryOffset = buf.readUInt32LE(o); o += 4;
    const entryLength = buf.readUInt32LE(o); o += 4;
    const term = buf.readUInt16LE(o); o += 2;
    if (term !== 0xFFFF) { console.error('bad term', term, ext, p, fn); process.exit(3); }
    const name = (p ? p + '/' : '') + fn + '.' + ext;
    if (pattern.test(name)) entries.push({ name, preload, archiveIndex, entryOffset, entryLength });
    if (preload > 0) o += preload;
    if (term === 0xFFFF) break;
  }
}
console.log(JSON.stringify({ vpk, version, entries }, null, 2));
