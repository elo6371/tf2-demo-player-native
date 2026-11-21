import struct, sys
def load(p):
    d=open(p,'rb').read(); off=struct.unpack_from('<I',d,10)[0]
    w,h=struct.unpack_from('<ii',d,18); bpp=struct.unpack_from('<H',d,28)[0]
    stride=((w*bpp//8)+3)//4*4; bottom=h>0; h=abs(h); rows=[]
    for y in range(h):
        ry=(h-1-y) if bottom else y
        rows.append(d[off+ry*stride: off+ry*stride+w*bpp//8])
    return w,h,bpp,rows
w,h,bpp,O=load(sys.argv[1]); _,_,_,B=load(sys.argv[2]); _,_,_,T=load(sys.argv[3])
by=bpp//8
stat={'vm':[0,0,0],'ent':[0,0,0]}  # n, clip_off, clip_base
full=0
for y in range(h):
    ro,rb,rt=O[y],B[y],T[y]
    for x in range(w):
        i=x*by
        d_ob=max(abs(ro[i]-rb[i]),abs(ro[i+1]-rb[i+1]),abs(ro[i+2]-rb[i+2]))
        d_bt=max(abs(rb[i]-rt[i]),abs(rb[i+1]-rt[i+1]),abs(rb[i+2]-rt[i+2]))
        co=(ro[i]==255 or ro[i+1]==255 or ro[i+2]==255)
        cb=(rb[i]==255 or rb[i+1]==255 or rb[i+2]==255)
        if d_ob>10 and d_bt<=2:
            s=stat['vm']; s[0]+=1; s[1]+=co; s[2]+=cb
        elif d_bt>10:
            s=stat['ent']; s[0]+=1; s[1]+=co; s[2]+=cb
for k,(n,co,cb) in stat.items():
    print("%-4s n=%6d clip_off=%6d (%6.3f%%) clip_base=%6d (%6.3f%%)" % (k,n,co,100.0*co/max(n,1),cb,100.0*cb/max(n,1)))
