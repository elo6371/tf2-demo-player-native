import struct
for fn in ("/tmp/pl_badwater.nav","/tmp/cp_snakewater_final1.nav"):
    d=open(fn,"rb").read()
    print("==",fn)
    pat=struct.pack("<4I",0,1,2,3)
    i=d.find(pat)
    print("  pattern 0,1,2,3 at",i)
    if i>=0:
        # find next id 4 position to infer stride
        for stride in range(40,200):
            if d[i+stride:i+stride+4]==struct.pack("<I",4) and d[i+2*stride:i+2*stride+4]==struct.pack("<I",8):
                print("  stride",stride)
                for k in range(3):
                    off=i+k*stride
                    fl=struct.unpack_from("<16f",d,off+4)
                    print("   rec",k,"off",off,"floats",[round(x,2) for x in fl])
                break
