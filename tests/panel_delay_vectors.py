"""Panel in headless Edge: alarm delay (short excursions), delayed start UI,
lowest battery voltage. Uses the preview page written by tools/gen_panel.py."""
import json
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(root / 'tools' / 'gen_panel.py')], check=True, capture_output=True)
edge = next(p for p in (r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
                        r'C:\Program Files\Microsoft\Edge\Application\msedge.exe') if pathlib.Path(p).exists())
preview = (root / 'tmp' / 'panel_preview.html').read_text(encoding='utf-8')
work = pathlib.Path(tempfile.mkdtemp(prefix='tlp_panel_delay_'))
checks = 0


def check(condition, label):
    global checks
    checks += 1
    assert condition, f'panel delay check {checks} failed: {label}'


def probe(delay, vbat):
    page = re.sub(r'"alarmDelay":\d+', f'"alarmDelay":{delay}', preview)
    page = re.sub(r'"vbatMin":\d+', f'"vbatMin":{vbat}', page)
    script = ("<script>addEventListener('load',()=>setTimeout(()=>{let r={};try{"
              "r.alarm=curEpisodes().length;r.short=curShortEpisodes().length;r.verdict=$('verdict').className;"
              "r.rows=$('alarmRows').children.length;r.shortRows=$('alarmRows').querySelectorAll('.shortexc').length;"
              "r.info=$('deviceInfo').innerHTML;"
              "$('startDelayMode').value='in';$('startDelayMode').onchange();r.minVisible=!$('startDelayMin').hidden;r.atHidden=$('startDelayAt').hidden;"
              "$('startDelayMode').value='at';$('startDelayMode').onchange();r.atVisible=!$('startDelayAt').hidden;"
              "setStartWait('2026-10-25 08:30:00');r.wait=$('startWaitInfo').textContent;"
              "applyLoggerStateResponse('LOGGER STATUS app=READY logger=IDLE records=0\\n','LOGGER STATUS');r.waitCleared=$('startWaitInfo').textContent;"
              "r.cfgText=configText(readCfg());"
              "let pd=(n)=>dtPop.querySelector(n).dispatchEvent(new PointerEvent('pointerdown',{bubbles:true}));$('cfgClock').value='2026-10-05T08:15';"
              "openPicker($('cfgClock'),$('cfgClock').parentNode);pd('[data-d=\"20\"]');pd('[data-a=\"h+\"]');document.dispatchEvent(new PointerEvent('pointerup'));pd('[data-a=\"ok\"]');r.picked=$('cfgClock').value;r.popClosed=!dtPop;"
              "let iv=$('cfgInterval'),before=+iv.value;iv.closest('.stepper').querySelector('.stepbtn.up').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true}));document.dispatchEvent(new PointerEvent('pointerup'));r.stepped=+iv.value-before;r.btnOrder=[...iv.closest('.stepper').children].map(e=>e.className||e.tagName).join('|');"
              "}catch(e){r.err=String(e)}document.body.setAttribute('data-probe',JSON.stringify(r))},600))</script>")
    page_file = work / f'p{delay}_{vbat}.html'
    page_file.write_text(page.replace('</body>', script + '</body>'), encoding='utf-8')
    dom = subprocess.run([edge, '--headless=new', '--disable-gpu', f'--user-data-dir={work / "profile"}',
                          '--virtual-time-budget=4000', '--dump-dom', page_file.as_uri()],
                         capture_output=True, text=True, encoding='utf-8').stdout
    match = re.search(r'data-probe="([^"]*)"', dom)
    assert match, 'probe result missing'
    return json.loads(match.group(1).replace('&quot;', '"').replace('&lt;', '<').replace('&gt;', '>').replace('&amp;', '&'))


r = probe(0, 2930)
check('err' not in r, r.get('err'))
check(r['alarm'] == 6 and r['short'] == 0 and r['verdict'] == 'verdict bad', 'no delay: every excursion is an alarm')
check('2.93 V' in r['info'] and 'class="bad"' not in r['info'].split('2.93 V')[1][:80], 'battery voltage row, no warning')
check(r['minVisible'] and r['atHidden'] and r['atVisible'], 'delayed start inputs follow the mode')
check(r['wait'].endswith('25.10.2026 09:30') and r['waitCleared'] == '', 'start time in device local time, cleared by status')
check('alarm_delay_min=0' in r['cfgText'], 'config text carries the delay')
check(r['picked'] == '2026-10-20T09:15' and r['popClosed'], 'calendar picks day and hour')
check(r['stepped'] == 1 and r['btnOrder'].startswith('INPUT') and 'down' in r['btnOrder'].split('|')[1], 'stepper: value, then - and + on the right')

r = probe(12, 2650)
check(r['alarm'] == 0 and r['short'] == 6 and r['verdict'] == 'verdict ok', '11-min excursions are short with a 12-min delay')
check(r['rows'] == 6 and r['shortRows'] == 6, 'short excursions listed, greyed')
check('2.65 V' in r['info'] and 'class="bad"' in r['info'], 'low battery flagged')
check('12 min' in r['info'], 'alarm delay shown in device info')

r = probe(10, 2930)
check(r['alarm'] == 6 and r['short'] == 0, '11-min excursions outlast a 10-min delay')

print(f'PASS panel alarm delay / delayed start / battery: {checks} checks in Edge')
