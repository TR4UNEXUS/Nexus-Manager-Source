// Host layout reference, NOT a Switch screenshot or a live error report.
// Uses the production dialog rectangle and labels; Canvas approximates NanoVG.
const fs=require('fs');
const path=require('path');
const assert=require('assert');
const {chromium}=require('playwright');
(async()=>{
 const root=path.resolve(__dirname,'..');
 const code=fs.readFileSync(path.join(root,'sphaira/source/ui/error_box.cpp'),'utf8');
 const match=code.match(/m_pos = \{(\d+),(\d+),(\d+),(\d+)\}/);
 assert(match,'production error panel rectangle missing');
 const rect=match.slice(1).map(Number);
 assert(rect[0]>=0 && rect[1]>=0 && rect[0]+rect[2]<=1280 && rect[1]+rect[3]<=720);
 assert(!code.includes('github.com/ITotalJustice'));
 const out=path.resolve(process.argv[2]||path.join(root,'build/dialog-preview.png'));
 const browser=await chromium.launch({headless:true,executablePath:process.env.NEXUS_PREVIEW_BROWSER||undefined});
 try {
  const page=await browser.newPage({viewport:{width:1280,height:720},deviceScaleFactor:1});
  await page.setContent('<body style="margin:0"><canvas width="1280" height="720"></canvas></body>');
  const result=await page.evaluate((r)=>{
   const ctx=document.querySelector('canvas').getContext('2d');
   ctx.fillStyle='#07101e';ctx.fillRect(0,0,1280,720);
   const [x,y,w,h]=r, accent='#43eee5', text='#e9f5fa', muted='#97b5c6';
   function round(x,y,w,h,r,fill,stroke){ctx.beginPath();ctx.roundRect(x,y,w,h,r);ctx.fillStyle=fill;ctx.fill();if(stroke){ctx.strokeStyle=stroke;ctx.lineWidth=1.5;ctx.stroke();}}
   function label(s,x,y,size,color){ctx.textBaseline='top';ctx.font=size+'px Arial';ctx.fillStyle=color;ctx.fillText(s,x,y);}
   const g=ctx.createLinearGradient(x,y,x+w,y+h);g.addColorStop(0,'#132839');g.addColorStop(1,'#0a1725');
   round(x,y,w,h,22,g,'#305467');round(x+28,y+1,78,3,1,accent);
   label('USB LOADER',x+32,y+26,14,accent);label('/  OPERATION REPORT',x+158,y+26,14,muted);
   label('Action stopped',x+32,y+60,34,text);
   label('Code 0x00001759   /   Module 345 (Libnx)   /   Description 11',x+32,y+110,19,muted);
   label('Invalid input / safety check stopped',x+32,y+142,18,text);
   ctx.fillStyle='#304b5b';ctx.fillRect(x+32,y+196,w-64,1);
   const message='Removal stopped at: Confirm destination launch records.\n\nDestination retained. No source files were removed.\n\nDetails: switch/usbloader/nexus.log';
   ctx.font='22px Arial';const lines=[];
   for(const para of message.split('\n')){let line='';for(const word of para.split(' ')){const s=line?line+' '+word:word;if(ctx.measureText(s).width>w-64){lines.push(line);line=word;}else line=s;}lines.push(line);}
   lines.forEach((line,i)=>label(line,x+32,y+217+i*27.5,22,text));
   round(x+32,y+h-86,w-64,54,12,'#1a4a53',accent);
   const btn='Close  /  A or B';ctx.font='22px Arial';label(btn,x+w/2-ctx.measureText(btn).width/2,y+h-69,22,accent);
   label('HOST LAYOUT PREVIEW — example stage, not a captured console failure',32,687,15,muted);
   return {lines:lines.length,bodyBottom:y+217+lines.length*27.5,buttonTop:y+h-86};
  },rect);
  assert(result.bodyBottom<result.buttonTop,'message overlaps dismissal button');
  await page.screenshot({path:out});
  console.log('Dialog layout preview passes screen bounds and message/button separation:',out);
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
