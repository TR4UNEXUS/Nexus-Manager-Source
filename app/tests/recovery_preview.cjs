// Host layout approximation of production OptionBox, NOT a console screenshot.
// Text fixtures come from the actual recovery adapter, not hand-written copies.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {chromium}=require('playwright');
(async()=>{
 const root=path.resolve(__dirname,'..'),out=path.join(root,'build/recovery-tests');
 const code=fs.readFileSync(path.join(root,'sphaira/source/ui/option_box.cpp'),'utf8');
 const width=+code.match(/BOX_WIDTH = (\d+)/)[1],height=+code.match(/BOX_HEIGHT = (\d+)/)[1];
 const bh=+code.match(/BUTTON_HEIGHT = (\d+)/)[1];
 const browser=await chromium.launch({headless:true,executablePath:process.env.NEXUS_PREVIEW_BROWSER});
 try{
  const page=await browser.newPage({viewport:{width:1280,height:720}});
  for(const name of ['summary','issue','long-issue']){
   const text=fs.readFileSync(path.join(out,name+'.txt'),'utf8');
   await page.setContent('<body style="margin:0"><canvas width="1280" height="720"></canvas></body>');
   const fit=await page.evaluate(({text,width,height,bh,name})=>{
    const ctx=document.querySelector('canvas').getContext('2d');const x=(1280-width)/2,y=(720-height)/2,ch=height-bh;
    ctx.fillStyle='#07101e';ctx.fillRect(0,0,1280,720);ctx.fillStyle='#132839';ctx.beginPath();ctx.roundRect(x,y,width,height,22);ctx.fill();
    function label(t,x,y,size,color){ctx.font=size+'px Arial';ctx.textBaseline='top';ctx.fillStyle=color;ctx.fillText(t,x,y);}
    function lines(text,size,w){ctx.font=size+'px Arial';const out=[];for(const para of text.split('\n')){let l='';for(const word of para.split(' ')){let n=l?l+' '+word:word;if(ctx.measureText(n).width>w && l){out.push(l);l='';}for(const char of (l?' ':'')+word){if(ctx.measureText(l+char).width>w && l){out.push(l);l='';}l+=char;}}out.push(l);}return out;}
    label('USB LOADER   /   RECOVERY RESULTS',x+32,y+26,14,'#43eee5');
    const first=text.indexOf('\n'),heading=text.slice(0,first),body=text.slice(first).replace(/^\n+/,'');
    const titleLines=lines(heading,26,width-60);titleLines.forEach((l,i)=>label(l,x+30,y+70+i*31.2,26,'#e9f5fa'));
    const by=y+70+titleLines.length*31.2+18,available=y+ch-by-24;
    let font=22,bodyLines;for(;font>14;--font){bodyLines=lines(body,font,width-60);if(bodyLines.length*font*1.25<=available)break;}
    bodyLines=lines(body,font,width-60);bodyLines.forEach((l,i)=>label(l,x+30,by+i*font*1.25,font,'#e9f5fa'));
    const left=name==='summary'?'Review issues':'Summary',right=name==='summary'?'Exit app':name==='issue'?'Summary':'Next issue';
    for(let i=0;i<2;++i){ctx.strokeStyle='#43eee5';ctx.strokeRect(x+16+i*width/2,y+ch+12,width/2-32,bh-30);label(i?right:left,x+50+i*width/2,y+ch+30,22,'#43eee5');}
    label('HOST LAYOUT PREVIEW - real production text, approximate font metrics; not console validation',28,690,14,'#97b5c6');
    ctx.font=font+'px Arial';
    return {font,lines:bodyLines.length,bottom:by+bodyLines.length*font*1.25,limit:y+ch-24,widest:Math.max(...bodyLines.map(l=>ctx.measureText(l).width)),width:width-60};
   },{text,width,height,bh,name});
   assert(fit.bottom<=fit.limit && fit.widest<=fit.width,name+' clips');assert(fit.font>=17,name+' becomes too small');
   await page.screenshot({path:path.join(out,name+'.png')});console.log(name,JSON.stringify(fit));
  }
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
