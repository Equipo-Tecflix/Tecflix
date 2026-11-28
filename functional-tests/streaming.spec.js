const {test,expect}=require('@playwright/test');
const {execFileSync}=require('node:child_process');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const base=process.env.BASE_URL||'http://localhost:8083';
const peer=process.env.PEER_URL;
test('upload, DASH, range, playback and replication',async({request,page})=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'tecflix-'));
 const file=path.join(dir,'test.mp4'); const id='pw-'+Date.now();
 try {
  execFileSync('ffmpeg',['-loglevel','error','-y','-f','lavfi','-i','testsrc2=size=640x360:rate=25','-f','lavfi','-i','sine=frequency=440','-t','12','-c:v','libx264','-threads','2','-pix_fmt','yuv420p','-c:a','aac',file]);
  const upload=await request.post(`${base}/videos/${id}/upload`,{data:fs.readFileSync(file),headers:{'Content-Type':'video/mp4'}});
  expect(upload.status()).toBe(202);
  for(const url of [base,...(peer?[peer]:[])]) {
   await expect.poll(async()=>{const r=await request.get(`${url}/videos/${id}/status`);return r.ok()?(await r.json()).status:'missing';},{timeout:120000}).toBe('ready');
   const mpd=await request.get(`${url}/videos/${id}/manifest.mpd`); expect(mpd.ok()).toBeTruthy(); expect(await mpd.text()).toContain('<MPD');
   const range=await request.get(`${url}/videos/${id}/init-0.m4s`,{headers:{Range:'bytes=0-15'}}); expect(range.status()).toBe(206); expect((await range.body()).length).toBe(16);
  }
  await page.route(`${base}/__test_player`,r=>r.fulfill({contentType:'text/html',body:'<video muted playsinline></video>'}));
  await page.goto(`${base}/__test_player`);
  await page.addScriptTag({path:require.resolve('shaka-player/dist/shaka-player.compiled.js')});
  await page.evaluate(async url=>{shaka.polyfill.installAll(); const v=document.querySelector('video'); const p=new shaka.Player(); await p.attach(v); await p.load(url); await v.play(); window.player=p;},`${base}/videos/${id}/manifest.mpd`);
  await expect.poll(()=>page.locator('video').evaluate(v=>v.currentTime),{timeout:15000}).toBeGreaterThan(1);
  await page.evaluate(()=>window.player.destroy());
 } finally {
  await request.delete(`${base}/videos/${id}`).catch(()=>{});
  fs.rmSync(dir,{recursive:true,force:true});
 }
});
