import fs from 'node:fs';
const dir = 'D:/_shared/t52-a3/a3pix/';
function readBmp(p){const b=fs.readFileSync(p);const off=b.readUInt32LE(10),w=b.readInt32LE(18),h=b.readInt32LE(22),bpp=b.readUInt16LE(28);return{b,off,w,h:Math.abs(h),bpp,bottomUp:h>0};}
function px(f,x,y){const row=f.bottomUp?(f.h-1-y):y;const stride=Math.round(f.w*f.bpp/8/4)*4;const i=f.off+row*stride+x*(f.bpp/8);return[f.b[i+2],f.b[i+1],f.b[i]];}
const trace = fs.readFileSync(dir+'on1.trace','utf8').split('\n');
const drawIdx = trace.map((l,i)=>[l,i]).filter(([l])=>l.includes('outline: draw ')).pop()[1];
const boxes=[];
for(let i=drawIdx+1;i<trace.length;i++){
  const l=trace[i];
  if(l.includes('outline: draw ')) break;
  const m=l.match(/outline: item entity=(\d+).*x0=(-?\d+) y0=(-?\d+) x1=(-?\d+) y1=(-?\d+).*onScreen=1/);
  if(m) boxes.push({e:+m[1],x0:+m[2],y0:+m[3],x1:+m[4],y1:+m[5]});
}
const inside=(x,y)=>boxes.some(b=>x>=b.x0&&x<=b.x1&&y>=b.y0&&y<=b.y1);
const A=readBmp(dir+'on1.bmp'),B=readBmp(dir+'bind1.bmp'),C=readBmp(dir+'bind_d.bmp');
function cmp(p,q){let inN=0,outN=0,maxd=0,minx=1e9,miny=1e9,maxx=-1,maxy=-1;
 for(let y=0;y<p.h;y++)for(let x=0;x<p.w;x++){const a=px(p,x,y),b=px(q,x,y);
  const d=Math.max(Math.abs(a[0]-b[0]),Math.abs(a[1]-b[1]),Math.abs(a[2]-b[2]));
  if(d>0){if(inside(x,y))inN++;else outN++;if(d>maxd)maxd=d;if(x<minx)minx=x;if(y<miny)miny=y;if(x>maxx)maxx=x;if(y>maxy)maxy=y;}}
 return {inside:inN,outside:outN,maxd,box:[minx,miny,maxx,maxy]};}
console.log('published_boxes='+boxes.length);
console.log('boxes='+boxes.map(b=>b.e+':'+b.x0+','+b.y0+'-'+b.x1+','+b.y1).join(' '));
console.log('determinism_bind_vs_binddup='+JSON.stringify(cmp(B,C)));
console.log('animation_on_vs_bind='+JSON.stringify(cmp(A,B)));
