'use strict';
const fs = require('node:fs');
const path = require('node:path');
const directory = path.resolve(process.argv[2]);
const root = path.resolve(directory, '../../..');
const {chromium} = require(path.join(root, 'thesis-documentation/evaluation/node_modules/playwright'));
async function main() {
  const analysis = JSON.parse(fs.readFileSync(path.join(directory, 'analysis.json')));
  const browser = await chromium.launch({executablePath:'/usr/bin/google-chrome',headless:true});
  try {
    const page = await browser.newPage({viewport:{width:1200,height:620},deviceScaleFactor:1});
    await page.setContent('<html><body style="margin:0"><canvas width="1200" height="620"></canvas></body></html>');
    await page.evaluate(data => {
      const c = document.querySelector('canvas').getContext('2d');
      c.fillStyle='#ffffff';c.fillRect(0,0,1200,620);
      const text=(s,x,y,size=16,color='#242424')=>{c.font=`${size}px Arial`;c.fillStyle=color;c.fillText(s,x,y);};
      text('F450 buffering A/B: pre-encoder queue waiting time',38,42,27);
      text('Three trials per policy and load; dots are trial p95s, lines connect paired repetitions.',38,73);
      const colors={fifo:'#bc4141',latest:'#087e8b'};
      for(const [panel,load] of ['normal','slow'].entries()) {
        const left=85+panel*580, top=140, width=430, height=330;
        const maximum=load==='normal'?.30:600;
        text(load==='normal'?'Normal load':'Controlled slowdown: 100 ms/frame',left-25,112,20);
        for(let i=0;i<=6;i++) {
          const y=top+height-height*i/6;
          c.strokeStyle='#e3e6e6';c.beginPath();c.moveTo(left,y);c.lineTo(left+width,y);c.stroke();
          text((maximum*i/6).toFixed(load==='normal'?2:0),left-50,y+5,14);
        }
        for(let rep=1;rep<=3;rep++) {
          const pair=data.trials.filter(t=>t.load===load&&t.replicate===rep);
          const a=pair.find(t=>t.profile==='fifo'),b=pair.find(t=>t.profile==='latest');
          c.strokeStyle='#b6bcbc';c.beginPath();
          c.moveTo(left+100,top+height-height*a.probe.queue_wait_ms.p95/maximum);
          c.lineTo(left+330,top+height-height*b.probe.queue_wait_ms.p95/maximum);c.stroke();
        }
        for(const [i,policy] of ['fifo','latest'].entries()) {
          const x=left+100+i*230;
          const trials=data.trials.filter(t=>t.load===load&&t.profile===policy);
          for(const t of trials) {
            c.fillStyle=colors[policy];c.beginPath();c.arc(x,top+height-height*t.probe.queue_wait_ms.p95/maximum,6,0,Math.PI*2);c.fill();
          }
          const mean=trials.reduce((sum,t)=>sum+t.probe.queue_wait_ms.p95,0)/trials.length;
          const y=top+height-height*mean/maximum;
          c.strokeStyle=colors[policy];c.lineWidth=3;c.beginPath();c.moveTo(x-20,y);c.lineTo(x+20,y);c.stroke();c.lineWidth=1;
          text(policy==='fifo'?'FIFO':'Latest-frame (2)',x-45,top+height+30,17);
          text(`Mean: ${mean.toFixed(3)} ms`,x-61,top+height+56,15,colors[policy]);
        }
      }
      text('Vertical axes: queue residence time (ms). Panel scales differ.',38,570,16);
      text('Not camera-to-screen latency. Earlier optical measurements are separate.',38,598,16);
    },analysis);
    await page.locator('canvas').screenshot({path:path.join(directory,'queue-p95-comparison.png')});
    console.log('Saved queue-p95-comparison.png');
  } finally {await browser.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
