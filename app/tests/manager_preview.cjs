// Render the C++ preview generated from production layout/icon geometry.
// Host layout reference only; browser fonts are not Switch NanoVG rendering.
const fs=require('fs');
const path=require('path');
const assert=require('assert');
const {chromium}=require('playwright');
(async()=>{
 const base=path.resolve(process.argv[2]);
 const browser=await chromium.launch({headless:true,executablePath:process.env.NEXUS_PREVIEW_BROWSER||undefined});
 try {
  const page=await browser.newPage({viewport:{width:1280,height:720},deviceScaleFactor:1});
  for(const source of [base,base+'.library.svg',...process.argv.slice(3).map(file=>path.resolve(file))]){
   await page.setContent('<html><body style="margin:0">'+fs.readFileSync(source,'utf8')+'</body></html>');
   await page.evaluate(()=>document.fonts.ready);
   const overflow=await page.evaluate(()=>[...document.querySelectorAll('foreignObject')].flatMap(f=>{
    const d=f.firstElementChild;
    return d.scrollHeight>f.height.baseVal.value+1?[d.textContent]:[];
   }));
   assert.deepStrictEqual(overflow,[],'wrapped body text exceeds its native layout rectangle');
   const destination=source.replace(/\.svg$/,'.png');
   await page.screenshot({path:destination});
   console.log('PASS host preview body bounds:',destination);
  }
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
