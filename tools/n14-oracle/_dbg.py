
import struct, sys, lzma
p = sys.argv[1]
blob = open(p,'rb').read()
def pair(i): return struct.unpack_from('<ii', blob, 8+i*16)
def dec(i):
    off,size = pair(i)
    if size<=0: return b'', 'empty', off, size
    raw = blob[off:off+size]
    if raw[:4]!=b'LZMA': return raw,'stored',off,size
    actual, lz = struct.unpack_from('<II', raw, 4)
    prop = raw[12]; lc=prop%9; r=prop//9; lp=r%5; pb=r//5
    d = struct.unpack_from('<I', raw, 13)[0]
    f=[{'id':lzma.FILTER_LZMA1,'lc':lc,'lp':lp,'pb':pb,'dict_size':d}]
    out = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=f).decompress(raw[17:17+lz], max_length=actual)
    return out,'lzma',off,size
for i,name,stride in [(1,'planes',20),(2,'texdata',32),(3,'vertexes',12),(6,'texinfo',72),(7,'faces',56),(12,'edges',4),(13,'surfedges',4),(43,'strdata',1),(44,'strtable',4)]:
    data,kind,off,size = dec(i)
    print(i, name, kind, 'fileoff', off, 'filesize', size, 'decoded', len(data), 'rem', len(data)%stride if stride>1 else 0)
