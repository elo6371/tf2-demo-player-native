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
def run(rect):
    x0,y0,x1,y1=rect
    vm=[]; ent=0
    for y in range(y0,min(y1,h)):
        ro,rb,rt=O[y],B[y],T[y]
        for x in range(x0,min(x1,w)):
            i=x*by
            d_ob=max(abs(ro[i]-rb[i]),abs(ro[i+1]-rb[i+1]),abs(ro[i+2]-rb[i+2]))
            d_bt=max(abs(rb[i]-rt[i]),abs(rb[i+1]-rt[i+1]),abs(rb[i+2]-rt[i+2]))
            if d_ob>10 and d_bt<=2:
                lo=(ro[i]*299+ro[i+1]*587+ro[i+2]*114)//1000
                lb=(rb[i]*299+rb[i+1]*587+rb[i+2]*114)//1000
                vm.append((lo,lb))
            elif d_bt>10:
                ent+=1
    n=len(vm)
    if not n:
        print("rect %s: VM-MASK-EMPTY (entity px=%d)"%(rect,ent)); return
    so=sum(p[0] for p in vm); sb=sum(p[1] for p in vm)
    d16o=sum(1 for p in vm if p[0]<16); d16b=sum(1 for p in vm if p[1]<16)
    d24o=sum(1 for p in vm if p[0]<24); d24b=sum(1 for p in vm if p[1]<24)
    br=sum(1 for p in vm if p[1]>p[0]); dk=sum(1 for p in vm if p[1]<p[0])
    ls=sorted(p[0] for p in vm); lb2=sorted(p[1] for p in vm)
    print("rect %d,%d,%d,%d vm_px=%d entity_px=%d"%(x0,y0,x1-x0,y1-y0,n,ent))
    print("  off  mean=%.2f p10=%d p50=%d p90=%d dark16=%.4f dark24=%.4f min=%d"%(so/n,ls[n//10],ls[n//2],ls[9*n//10],d16o/n,d24o/n,ls[0]))
    print("  base mean=%.2f p10=%d p50=%d p90=%d dark16=%.4f dark24=%.4f min=%d"%(sb/n,lb2[n//10],lb2[n//2],lb2[9*n//10],d16b/n,d24b/n,lb2[0]))
    print("  delta=%+.2f brighter=%d darker=%d same=%d"%(sb/n-so/n,br,dk,n-br-dk))
for a in sys.argv[4:]:
    run([int(v) for v in a.split(',')])
