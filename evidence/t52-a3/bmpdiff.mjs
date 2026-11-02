import fs from 'node:fs';
function readBmp(p){
  const b = fs.readFileSync(p);
  const off = b.readUInt32LE(10), w = b.readInt32LE(18), h = b.readInt32LE(22), bpp = b.readUInt16LE(28);
  return {b, off, w, h: Math.abs(h), bpp, bottomUp: h>0};
}
function px(f, x, y){
  const row = f.bottomUp ? (f.h-1-y) : y;
  const stride = Math.round(f.w*f.bpp/8/4)*4;
  const i = f.off + row*stride + x*(f.bpp/8);
  return [f.b[i+2], f.b[i+1], f.b[i]];
}
function diff(a,b){
  let n=0, maxd=0, minx=1e9,miny=1e9,maxx=-1,maxy=-1;
  for(let y=0;y<a.h;y++) for(let x=0;x<a.w;x++){
    const p=px(a,x,y), q=px(b,x,y);
    const d=Math.max(Math.abs(p[0]-q[0]),Math.abs(p[1]-q[1]),Math.abs(p[2]-q[2]));
    if(d>0){ n++; if(d>maxd)maxd=d; if(x<minx)minx=x; if(y<miny)miny=y; if(x>maxx)maxx=x; if(y>maxy)maxy=y; }
  }
  return {n,maxd,box:[minx,miny,maxx,maxy]};
}
function mean(f){let s=0;for(let y=0;y<f.h;y++)for(let x=0;x<f.w;x++){const p=px(f,x,y);s+=(p[0]+p[1]+p[2])/3;}return +(s/(f.w*f.h)).toFixed(2);}
const dir = process.argv[2] || 'D:/_shared/t52-a3/a3pix/';
const load = n => readBmp(dir + n + '.bmp');
const A = load('on1'), B = load('bind1'), C = load('bind_d');
console.log('dim', A.w, A.h, A.bpp);
console.log('bind1_vs_bind_d', JSON.stringify(diff(B,C)));
console.log('on1_vs_bind1', JSON.stringify(diff(A,B)));
console.log('mean on1', mean(A), 'bind1', mean(B));
