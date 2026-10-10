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
  check(JSON.stringify(r.menuOrder) === '[0,1,4,3,6,2,5,7,8,9,10,11]', 'menu checkboxes in device order');
  check(JSON.stringify(r.startOrder) === '[0,1,4,3,6,2,5,7,8,9,10,11]', 'start screen options in device order');
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
fs.rmSync(dir, {recursive: true, force: true});
console.log(`PASS panel in Edge: ${checks} checks at 1440 and 480 px`);
