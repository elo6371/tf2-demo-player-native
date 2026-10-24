import struct
for fn,name in (("/tmp/pl_badwater.nav",b"pl_badwater"),("/tmp/cp_snakewater_final1.nav",b"cp_snakewater_final1")):
    d=open(fn,"rb").read()
    i=d.find(name)
    print("==",fn,"size",len(d),"name at",i)
    print("  bytes 16..96:",d[16:96].hex(" "))
    # try uint32s
    print("  u32@16..60:",[hex(x) for x in struct.unpack_from("<11I",d,16)])
    print("  f32@32..60:",[round(x,3) for x in struct.unpack_from("<7f",d,32)])
