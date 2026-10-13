/* Checks the embedded HTML panel in headless Microsoft Edge (no Playwright).
 *
 * Builds the page exactly as the firmware serves it (HTML_PREFIX + CSV + config
 * JSON + HTML_SUFFIX from Src/templogpro_panel_html.inc), appends a probe script
 * and reads its result from Edge's --dump-dom output.
 * Run: node tests/panel_edge_vectors.cjs
 */
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const {execFileSync} = require('node:child_process');

const root = path.resolve(__dirname, '..');
execFileSync('python', [path.join(root, 'tools/gen_panel.py'), '--check'], {stdio: 'inherit'});

const fwVersion = fs.readFileSync(path.join(root, 'Inc/templogpro_version.h'), 'utf8').match(/TEMPLOGPRO_FW_VERSION\s+"([^"]+)"/)[1];
const source = fs.readFileSync(path.join(root, 'Src/templogpro_panel_html.inc'), 'utf8');
function literal(name) {
  const block = source.match(new RegExp('static const char ' + name + '\\[\\] =([\\s\\S]*?);\\r?\\n'))[1];
  return [...block.matchAll(/"(?:\\.|[^"\\])*"/g)].map(x => JSON.parse(x[0])).join('');
}

const csv = 'Sequence,Date,Time,Temperature_C,Code\r\n' +
  '00001,2026-09-20,04:00:00,+000.000,R001\r\n' +
  '00002,2026-09-20,04:00:10,+026.000,M000\r\n' +
  '00003,1970-01-01,00:00:00,+000.000,E001\r\n' +
  '00004,2026-09-20,04:00:20,+026.062,M000\r\n' +
  '00005,2026-09-20,04:00:30,+033.000,M000\r\n';
const config = {interval: 10, low: 24, high: 32, battery: 1, lcd: 0, pdf: 1, led: 0, usbLogging: 1,
                lcdMenu: 4095 & ~(1 << 8), records: 5, capacity: 1000, unit: 1};
const probe = `<script>setTimeout(()=>{let r={};try{
  r.unit=$('cfgUnit').value;r.reportUnit=tempUnit;r.payload=serialPayload();r.intervalMax=$('cfgInterval').max;
  r.menuOrder=[...document.querySelectorAll('.lcd-menu-options input')].map(e=>+e.value);
  r.menuChecked=[...document.querySelectorAll('.lcd-menu-options input')].filter(e=>e.checked).length;
  r.homeLocked=$('cfgMenu0').disabled;
  r.startOrder=[...$('cfgLcd').options].map(o=>+o.value);
  r.rows=$('rows').children.length;r.badRecords=badRecords;
  r.episodes=curEpisodes().map(e=>e.dir+(e.open?'open':'')+e.peak.t);r.alarmRows=$('alarmRows').children.length;
  r.verdict=$('verdict').className;r.kpis=$('summary').children.length;
  r.eventCodes=[...$('eventRows').querySelectorAll('td:nth-child(4)')].map(e=>e.textContent);
  r.eventTemp=[...$('rows').children].find(e=>e.textContent.includes('R001')).children[3].textContent;
  rowFilter='out';renderRows();r.outRows=$('rows').children.length;rowFilter='all';renderRows();
  $('presetCustom').click();r.customOpen=!$('customRange').hidden&&$('presetCustom').classList.contains('on');$('rangeReset').click();r.customClosed=$('customRange').hidden;
  r.memPct=$('memPct').textContent;r.memBar=$('memBar').style.width;r.verdictText=$('verdict').textContent;
  r.signHidden=getComputedStyle(document.querySelector('.signbox')).display==='none';$('noteText').value='Towar OK';$('noteText').dispatchEvent(new Event('input'));$('checkedBy').value='Jan Kowalski';$('checkedBy').dispatchEvent(new Event('input'));r.signNotes=$('signNotesVal').textContent;r.signBy=$('signByVal').textContent;r.signBoxes=document.querySelectorAll('.signbox>div').length;r.noCard=!document.querySelector('.notecard')&&!$('printDlg').open;printSelected();r.dlgOpen=$('printDlg').open;$('printDlg').close('cancel');
  printAll=true;renderRows();r.printRows=$('printRows').querySelectorAll('div:not(.pday)').length;r.printDays=$('printRows').querySelectorAll('.pday').length;r.printFirst=$('printRows').children[1].textContent;printAll=false;renderRows();
  r.deviceInfo=$('deviceInfo').textContent;
  r.techClosed=!document.querySelector('details.techdetails').open;
  r.overflow=document.documentElement.scrollWidth-document.documentElement.clientWidth;
  r.asciiPolish=/urzadz|Polacz|Wyslij|Wyczysc|Gorny|bialy/.test(document.body.innerText);
}catch(e){r.error=e.message}document.body.setAttribute('data-probe',JSON.stringify(r))},200)</script>`;
const html = literal('HTML_PREFIX') + csv + '</script><script id="config-data" type="application/json">' +
  JSON.stringify(config) + '</script><script>' + literal('HTML_SUFFIX') + probe;

for (const m of html.matchAll(/<script>([\s\S]*?)<\/script>/g)) new vm.Script(m[1]);

const edge = ['C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
              'C:/Program Files/Microsoft/Edge/Application/msedge.exe'].find(p => fs.existsSync(p));
if (!edge) throw new Error('Microsoft Edge not found');
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ttpanel-'));
const page = path.join(dir, 'panel.html');
fs.writeFileSync(page, html);

function run(width) {
  const dom = execFileSync(edge, ['--headless=new', '--disable-gpu', `--user-data-dir=${path.join(dir, 'profile')}`,
    `--window-size=${width},1000`, '--virtual-time-budget=4000', '--dump-dom', 'file:///' + page.replace(/\\/g, '/')],
    {encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore']});
  const m = dom.match(/data-probe="([^"]*)"/);
  assert.ok(m, 'probe result missing');
  return JSON.parse(m[1].replace(/&quot;/g, '"').replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>'));
}

let checks = 0;
const check = (cond, msg) => { assert.ok(cond, msg); checks++; };
for (const width of [1440, 480]) {
  const r = run(width);
  check(!r.error, 'script error: ' + r.error);
  check(r.unit === '1' && r.reportUnit === 'F', 'device unit F drives the field and the report');
  check(r.payload.includes('temperature_unit=F\n') && r.payload.includes('lcd_menu_mask=3839\n'), 'payload');
  check(r.intervalMax === '600', 'interval limit');
  check(JSON.stringify(r.menuOrder) === '[0,12,1,4,3,6,2,5,7,8,9,10,11]', 'menu checkboxes in device order (clock after temperature)');
  check(JSON.stringify(r.startOrder) === '[0,12,1,4,3,6,2,5,7,8,9,10,11]', 'start screen options in device order');
  check(r.menuChecked === 11 && r.homeLocked, 'menu mask applied, home screen locked');
  check(r.rows === 4 && r.badRecords === 1, 'skipped E001 slot hidden from the table and counted');
  check(JSON.stringify(r.episodes) === '["Hopen33"]' && r.alarmRows === 1, 'one ongoing excursion above the limit, peak 33.0');
  check(r.verdict === 'verdict bad' && r.kpis === 6, 'verdict and KPI tiles');
  check(r.verdictText.includes('91.40') && r.verdictText.includes('78.80') && !/\d+ s\b/.test(r.verdictText), 'verdict names max and min, no durations');
  check(r.customOpen && r.customClosed, 'Custom range toggles the date fields');
  check(r.signHidden && r.signNotes === 'Towar OK' && r.signBy === 'Jan Kowalski' && r.signBoxes === 2 && r.noCard && r.dlgOpen, 'notes asked in the PDF dialog only, print box filled from it');
  check(r.memPct === '99.5 %' && r.memBar === '99.5%', 'free memory value and bar');
  check(JSON.stringify(r.eventCodes) === '["R001"]', 'events: R001 before the first sample is listed, E001 is not');
  check(r.eventTemp === '—', 'event rows show no temperature');
  check(r.outRows === 1 && r.printRows === 4 && r.printDays === 1 && r.printFirst.startsWith('1 04:00:00'), 'out-of-range filter; full print lists every entry oldest first');
  check(r.deviceInfo.includes('Pominięte wpisy') && r.deviceInfo.includes(fwVersion), 'device info');
  check(r.techClosed, 'technical details folded');
  check(r.overflow <= 0, `no horizontal overflow at ${width}px (${r.overflow}px)`);
  check(!r.asciiPolish, 'Polish text has diacritics');
}
// Time zone: Europe/Warsaw log across the switch to winter time (2026-10-25 01:00 UTC),
// local 02:00-02:59 appears twice (+02:00, then +01:00).
const tzRows = [['00001', '2026-10-25', '02:40:00', '+000.000', 'R001', '+02:00']];
for (let i = 0; i < 6; i++) {
  const utc = Date.UTC(2026, 9, 25, 0, 40 + i * 10), off = utc < Date.UTC(2026, 9, 25, 1) ? 120 : 60;
  const d = new Date(utc + off * 60000), p2 = n => String(n).padStart(2, '0');
  tzRows.push([String(i + 2).padStart(5, '0'), `${d.getUTCFullYear()}-${p2(d.getUTCMonth() + 1)}-${p2(d.getUTCDate())}`,
    `${p2(d.getUTCHours())}:${p2(d.getUTCMinutes())}:00`, '+025.000', 'M000', off === 120 ? '+02:00' : '+01:00']);
}
const tzCsv = 'Sequence,Date,Time,Temperature_C,Code,UTC_Offset\r\n' + tzRows.map(r => r.join(',')).join('\r\n') + '\r\n';
const tzConfig = {interval: 600, low: 2, high: 30, battery: 1, lcd: 0, led: 1, usbLogging: 1, lcdMenu: 4095, records: 7,
                  capacity: 1000, unit: 0, tzName: 'Europe/Warsaw', tzStd: 60, tzDst: 60, tzStart: [3, 5, 0, 120], tzEnd: [10, 5, 0, 180]};
const tzProbe = `<script>setTimeout(()=>{let r={};try{
  r.tzEvents=tzEvents.map(e=>e.code+':'+e.from+'>'+e.to);
  r.monotonic=temps.every((q,i)=>!i||q.ms>temps[i-1].ms);
  r.dupLocal=temps.filter(q=>q.time.startsWith('02:')).length;
  r.badge=$('tzBadge').textContent;
  r.roundTrip=temps.every(q=>{setInputMs('fromDt',q.ms);return readInputMs($('fromDt'))===q.ms});r.typed=inputToMs('2026-10-25T04:00:00')===Date.UTC(2026,9,25,3);
  r.zoneRule=JSON.stringify(zoneRule('Europe/Warsaw'));
  $('cfgTz').value='Europe/Warsaw';$('cfgDstMode').value='auto';$('cfgTz').onchange();r.payload=serialPayload('2026-07-01 10:00:00');r.noClock=clockToSend()==='';$('syncClockBtn').click();r.pcClock=Math.abs(Date.parse(clockToSend().replace(' ','T')+'Z')-Date.now())<5000;$('clockSource').value='manual';$('cfgClock').value='2026-07-01T12:00:00';r.manualClock=clockToSend();devClock={utc:Date.now()-5000,off:120,at:performance.now()};renderDevClock();r.devClock=$('devClock').textContent;
  r.clockUtc=utcStamp(clockToUtc('2026-07-01T12:00:00',uiZone()));
  r.ny=JSON.stringify(zoneRule('America/New_York'));r.syd=JSON.stringify(zoneRule('Australia/Sydney'));
  r.legend=$('chartBigLegend').textContent;r.info=$('deviceInfo').textContent;
  r.markers=[...document.querySelectorAll('#chartBig text')].filter(e=>/CZAS ZIMOWY/.test(e.textContent)).length;r.texts=[...document.querySelectorAll('#chartBig text')].map(e=>e.textContent).join('|').slice(0,300);
}catch(e){r.error=e.message+' '+e.stack}document.body.setAttribute('data-probe',JSON.stringify(r))},300)</script>`;
fs.writeFileSync(page, literal('HTML_PREFIX') + tzCsv + '</script><script id="config-data" type="application/json">' +
  JSON.stringify(tzConfig) + '</script><script>' + literal('HTML_SUFFIX') + tzProbe);
{
  const z = run(1440);
  check(!z.error, 'tz script error: ' + z.error);
  check(JSON.stringify(z.tzEvents) === '["Z002:120>60"]', 'one switch to winter time detected: ' + JSON.stringify(z.tzEvents));
  check(z.monotonic && z.dupLocal >= 4, 'repeated local hour stays in order on the UTC timeline');
  check(z.badge.includes('UTC+01:00') && z.badge.includes('Europe/Warsaw'), 'time zone badge: ' + z.badge);
  check(z.roundTrip && z.typed, 'range fields keep exact instants in the repeated hour; typed local time converts to UTC');
  check(z.zoneRule === '{"name":"Europe/Warsaw","std":60,"dst":60,"start":[3,5,0,120],"end":[10,5,0,180]}', 'EU rule derived from the browser: ' + z.zoneRule);
  check(z.ny === '{"name":"America/New_York","std":-300,"dst":60,"start":[3,2,0,120],"end":[11,1,0,120]}', 'US rule: ' + z.ny);
  check(z.syd === '{"name":"Australia/Sydney","std":600,"dst":60,"start":[10,1,0,120],"end":[4,1,0,180]}', 'AU rule: ' + z.syd);
  check(z.payload.includes('SET TIME 2026-07-01 10:00:00\n') && z.payload.includes('tz_name=Europe/Warsaw\n') &&
        z.payload.includes('tz_offset_min=60\n') && z.payload.includes('tz_dst_start=3.5.0.120\n') && z.payload.includes('tz_dst_end=10.5.0.180\n'), 'payload carries UTC time and the zone');
  check(z.clockUtc === '2026-07-01 10:00:00', 'manual local clock converted to UTC: ' + z.clockUtc);
  check(z.noClock && z.pcClock && z.manualClock === '2026-07-01 10:00:00', 'clock sent only when ticked, from the computer or the manual field: ' + [z.noClock, z.pcClock, z.manualClock].join(' '));
  check(z.devClock.includes('(UTC+02:00)') && z.devClock.includes('5 s'), 'live device clock with difference from the computer: ' + z.devClock);
  check(z.legend.includes('ZMIANA CZASU') && z.markers === 1, 'chart marks the time change: ' + z.legend + ' markers=' + z.markers + ' texts=' + z.texts);
  check(z.info.includes('Europe/Warsaw') && z.info.includes('ostatnia niedziela marca 02:00'), 'device info describes the zone and rule');
}
fs.rmSync(dir, {recursive: true, force: true});
console.log(`PASS panel in Edge: ${checks} checks at 1440 and 480 px, time zone scenario`);
