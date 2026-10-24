import struct
for fn,count in (("/tmp/pl_badwater.nav",1790),("/tmp/cp_snakewater_final1.nav",1707)):
    d=open(fn,"rb").read()
    print("==",fn,"size",len(d),"assumed areas",count)
    for k in range(3):
        off=32+96*k
        fl=struct.unpack_from("<24f",d,off)
        print("  area",k,"off",off,[round(x,2) for x in fl])
