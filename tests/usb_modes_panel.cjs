/* Render the firmware's actual embedded HTML with a small fixture. */
const fs = require('node:fs');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const assert = require('node:assert/strict');
const {chromium} = require('playwright');
const root = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'Src/templogpro_panel_html.inc'), 'utf8');
function literal(name) {
  const block = source.match(new RegExp('static const char '+name+'\\[\\] =([\\s\\S]*?);\\r?\\n'))[1];
  return [...block.matchAll(/"(?:\\.|[^"\\])*"/g)].map(x => JSON.parse(x[0])).join('');
}
const fixture = 'Sequence,Date,Time,Temperature_C,Code\r\n1,2026-09-20,04:00:00,0,R001\r\n2,2026-09-20,04:00:10,26,M000\r\n3,2026-09-20,04:00:20,26.0625,M000\r\n';
const config = {interval:10,low:24,high:32,battery:1,lcd:0,pdf:1,led:0,usbLogging:1,lcdMenu:4095,records:100,capacity:1000};
const html = literal('HTML_PREFIX')+fixture+'</script><script id="config-data" type="application/json">'+JSON.stringify(config)+'</script><script>'+literal('HTML_SUFFIX');
const output = path.join(root, 'tmp/usb_modes_panel.html');
fs.writeFileSync(output, html);
(async () => {
  const browser = await chromium.launch({channel:'msedge',headless:true});
  try {
    for (const viewport of [{width:1440,height:1000},{width:390,height:844}]) {
      const page = await browser.newPage({viewport});
      const errors=[];
      page.on('pageerror',e=>errors.push(e.message));
      await page.goto(pathToFileURL(output).href);
      await page.locator('#tabUsbBtn').click();
      assert.equal(await page.locator('.lcd-menu-options input').count(),12);
      assert.equal(await page.locator('#cfgMenu0').isDisabled(),true);
      assert.match(await page.locator('#storageInfo').innerText(),/90\.0%/);
      await page.locator('#cfgLcd').selectOption('3');
      await page.locator('#cfgMenu3').uncheck();
      assert.equal(await page.locator('#cfgLcd').inputValue(),'0');
      assert.match(await page.locator('#configText').inputValue(),/lcd_menu_mask=4087\n/);
      assert.match(await page.evaluate(()=>serialPayload()),/lcd_menu_mask=4087\n/);
      await page.evaluate(()=>applyConfigResponse('lcd_menu_mask=5\nlcd_mode=2\n'));
      assert.equal(await page.locator('#cfgMenu2').isChecked(),true);
      assert.equal(await page.locator('#cfgMenu3').isChecked(),false);
      assert.equal(await page.locator('#cfgLcd').inputValue(),'2');
      assert.equal(await page.locator('#cfgLcd option[value="10"]').innerText(),'Bateria');
      assert.equal(await page.locator('#cfgUsbMode').inputValue(),'1');
      await page.locator('#cfgUsbMode').selectOption('0');
      assert.match(await page.locator('#configText').inputValue(), /usb_logging=0\n/);
      assert.match(await page.evaluate(()=>serialPayload()), /usb_logging=0\n/);
      await page.evaluate(()=>applyConfigResponse('usb_logging=1\n'));
      assert.equal(await page.locator('#cfgUsbMode').inputValue(),'1');
      await page.locator('#langBtn').click();
      assert.equal(await page.locator('#cfgUsbModeLabel .labeltext').innerText(),'USB operating mode');
      assert.equal(await page.locator('#cfgMenuTitle').innerText(),'LCD menu screens');
      assert.equal(await page.locator('#cfgMenu2').isChecked(),true);
      assert.equal(await page.locator('#cfgMenu3').isChecked(),false);
      await page.locator('#langBtn').click();
      const fit=await page.locator('#cfgUsbMode').evaluate(e=>{
        const r=e.getBoundingClientRect(),p=e.parentElement.getBoundingClientRect();
        return r.left>=p.left && r.right<=p.right && r.left>=0 && r.right<=innerWidth;
      });
      assert(fit, 'USB selector exceeds field or viewport');
      assert(await page.locator('.lcd-menu-options').evaluate(e=>{
        const r=e.getBoundingClientRect();return r.left>=0&&r.right<=innerWidth;
      }), 'LCD menu exceeds viewport');
      assert.equal(await page.evaluate(()=>{cfg.records=1000;return freeMemoryText().startsWith('0.0%')}),true);
      assert.equal(await page.evaluate(()=>{cfg.records=0;return freeMemoryText().startsWith('100.0%')}),true);
      assert.equal(await page.evaluate(()=>{cfg.capacity=0;return freeMemoryText()}),'-');
      await page.evaluate(()=>{cfg.capacity=1000;cfg.records=100;renderInfo()});
      // Simulate split serial replies: CONFIG BEGIN must never be mistaken
      // for successful persistence. The subsequent status must update RUN.
      await page.evaluate(async()=>{
        const replies=['OK CONFIG BEGIN\n','OK CONFIG\n'];
        let writes=[];
        serialPort={
          writable:{getWriter:()=>({write:async bytes=>{let s=new TextDecoder().decode(bytes);writes.push(s);if(s==='LOGGER STATUS\n')replies.push('LOGGER STATUS app=PAUSE logger=STOPPED records=3\n')},releaseLock(){}})},
          readable:{getReader:()=>({read:async()=>replies.length?{value:new TextEncoder().encode(replies.shift()),done:false}:{done:true},cancel(){},releaseLock(){}})}
        };
        await sendConfig();
        if(writes.length!==2 || state!=='STOP')throw Error('config confirmation/status refresh failed');
      });
      assert.deepEqual(errors,[]);
      await page.screenshot({path:path.join(root,`tmp/usb_modes_${viewport.width}.png`),fullPage:true});
      await page.close();
      console.log(`PASS settings ${viewport.width}px: selector, PL/EN, serialization, config readback, split CDC responses`);
    }
  } finally { await browser.close(); }
})().catch(e=>{console.error(e);process.exitCode=1});
