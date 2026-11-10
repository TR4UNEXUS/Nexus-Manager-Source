// Host-rendered layout reference with sample capacities, not a Switch capture.
// Canvas approximates NanoVG; validates production geometry and footer fit.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {chromium}=require('playwright');
(async()=>{
 const root=path.resolve(__dirname,'..');
 const src=fs.readFileSync(path.join(root,'sphaira/source/ui/menus/nexus_menu.cpp'),'utf8');
 assert(src.includes('const float y=174+(i-start)*60;const Vec4 row{54,y,1172,56}'));
 assert(src.includes('nvgScissor(vg,128,y,862,55)'));
 const browser=await chromium.launch({headless:true,executablePath:process.env.NEXUS_PREVIEW_BROWSER||undefined});
 try {
  const page=await browser.newPage({viewport:{width:1280,height:720},deviceScaleFactor:1});
  await page.setContent('<body style="margin:0"><canvas width="1280" height="720"></canvas></body>');
  for(const mode of ['files','queue','results']){
   const info=await page.evaluate((mode)=>{
    const ctx=document.querySelector('canvas').getContext('2d');
    const accent='#43eee5',fg='#edf6ff',muted='#95abc2';
    function rect(x,y,w,h,r,color,stroke){ctx.beginPath();ctx.roundRect(x,y,w,h,r);ctx.fillStyle=color;ctx.fill();if(stroke){ctx.lineWidth=2;ctx.strokeStyle=stroke;ctx.stroke();}}
    function text(s,x,y,size=20,color=fg,align='left'){ctx.font=size+'px Arial';ctx.fillStyle=color;ctx.textBaseline='top';ctx.textAlign=align;ctx.fillText(s,x,y);}
    ctx.fillStyle='#07101e';ctx.fillRect(0,0,1280,720);rect(0,96,1280,550,0,'#0a1829');
    text('USB LOADER',54,27,28);text('NEXUS / 0.4.0',55,64,13,muted);
    ['microSD','System','USB'].forEach((n,i)=>{const x=600+i*210;rect(x,16,198,73,13,'#122538');text(n,x+14,25,15,accent);text(['83.2 GB free','54.3 GB free','391.5 GB free'][i],x+14,47,17);rect(x+14,74,170,4,2,'#25394d');rect(x+14,74,[62,10,30][i],4,2,accent);});
    const results=mode==='results',queue=mode==='queue';
    text(results?'Batch stopped':queue?'Install queue':'USB source',54,111,26);
    text(results?'2 installed / 1 failed / 0 cancelled / 4 not attempted   |   USB to SD / 0x00001759':queue?'Selection order is install order. Use L/R to arrange base, update, then DLC.':'/Packages',54,145,16,muted);
    const names=['Example Game [Base].nsp','Example Game [Update].NSZ','Example Game [DLC 1].nsp','Example Game [DLC 2].nsp','Another title [Base].nsp','Another title [Update].nsp','A very long package name that should clip inside the row rather than overlap its status field.nsp'];
    for(let i=0;i<7;i++){
     const y=174+i*60;if(i===2)rect(54,y,1172,56,0,'#0a1829',accent);
     if(mode==='files'){rect(72,y+14,30,28,6,'#1d5658');text(''+(i+1),87,y+20,13,accent,'center');}
     else text(''+(i+1),72,y+18,18,accent);
     ctx.save();ctx.beginPath();ctx.rect(128,y,862,55);ctx.clip();text(names[i],128,y+18,20);ctx.restore();
     text(results?['Installed','Installed','Failed','Not attempted','Not attempted','Not attempted','Not attempted'][i]:'204.5 MB',1208,y+18,17,results&&i===2?'#ff9393':muted,'right');
    }
    const detail=results?'Stage: Install package / A: file result / Saved: switch/usbloader/batch-last.log':queue?'Source files: 1.4 GB (not installed size) / +: choose SD, Internal Storage or USB':'Select base, update and DLC with Y, then + to review the queue and choose one destination.';
    text(detail,54,608,16,muted);
    rect(30,653,1220,52,14,'#112234');text(mode==='files'?'7 selected':'3 / 7',50,673,15,muted);
    const hints=mode==='files'?[['B','Back'],['A','Open'],['X','All'],['Y','Select'],['+','Queue'],['-','Refresh']]:queue?[['B','Back'],['A','Details'],['Y','Remove'],['R','Later'],['L','Earlier'],['+','Install']]:[['B','Back'],['A','Details'],['+','Remaining']];
    let x=1220;
    for(const [key,label] of hints){ctx.font='20px Arial';const w=ctx.measureText(label).width;text(label,x,675,20,fg,'right');x-=w+8;text(key,x,671,26,accent,'right');x-=26+34;}
    return {footerLeft:x,lastRowBottom:174+6*60+56};
   },mode);
   assert(info.footerLeft>255,'footer overlaps selection counter');
   assert(info.lastRowBottom<602,'rows overlap stage text');
   const out=path.join(root,'build/batch-'+mode+'-preview.png');
   await page.screenshot({path:out});console.log(mode,info,out);
  }
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
