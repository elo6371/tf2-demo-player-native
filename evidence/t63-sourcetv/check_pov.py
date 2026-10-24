import json,sys,struct
sys.path.insert(0,'D:/TF2_Native_Test/evidence/t63-sourcetv')
from packet_clock import walk_commands
p='D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-09-10_14-04-13.dem'
b=open(p,'rb').read()
hdr,packets,commands=walk_commands(b)
print('map',hdr['map'],'ticks',hdr['ticks'],'frames',hdr['frames'],'packets',len(packets),'first',[t for t,_,_ in packets[:6]],'last',packets[-1][0])
tm={t:(off,size) for t,off,size in packets}
for want in (600,601,602,603):
    if want in tm:
        off,size=tm[want]
        raw=b[off-88:off-12]
        fl=struct.unpack('<19f',raw)
        print('tick',want,'nonzero',sum(1 for x in raw if x),'origin',[round(x,3) for x in fl[1:4]],'angles',[round(x,3) for x in fl[4:6]])
maps=json.load(open('D:/TF2_Native_Test/evidence/t63-sourcetv/map_summary.json'))['maps']
print('bsp',maps.get(hdr['map']))
