"""ARM regressions for cancellation, persistent menu selection and capacity."""
import json
import struct
from lcd_menu_vectors import Display
from usb_modes_vectors import Device, symbols, OUT, ARG

checks = 0


def check(condition):
    global checks
    checks += 1
    assert condition, f'action/menu check {checks} failed'


class ActionDevice(Display):
    def __init__(self, state, action):
        super().__init__()
        self.held = action if action < 3 else 3
        self.formats = 0
        self.set('g_app_state', state)
        self.set('g_lcd_view', 11 if action == 3 else 2)
        self.set('g_next_measurement_timestamp', self.ts + 1000)
        self.addresses[symbols['templogpro_buttons_current_mask']] = lambda a,b,c: self.held
        self.addresses[symbols['templogpro_logger_format']] = self.format
        self.addresses[symbols['templogpro_power_read_vdd_mv']] = self.read_vdd

    def format(self, a, b, c):
        self.formats += 1
        return 1


for action, state in ((1,0), (1,2), (2,1), (3,0), (3,1), (3,2)):
    for steps in range(6):
        d = ActionDevice(state, action)
        view = d.get('g_lcd_view')
        d.call('app_start_action', action, d.tick)
        for _ in range(steps):
            d.advance(400)
            d.call('templogpro_app_process')
        d.held = 0
        d.advance(400)
        d.call('templogpro_app_process')
        check(d.get('g_action_active') == 0 and d.get('g_pending_action') == 0)
        check(d.get('g_app_state') == state and d.get('g_lcd_view') == view)
        check((d.starts, d.stops, d.formats) == (0,0,0))
        check(d.get('g_next_measurement_timestamp') == d.ts + 1000)
        d.advance(4000)
        d.call('templogpro_app_process')
        check((d.starts, d.stops, d.formats) == (0,0,0))

for action, state, expected in ((1,0,(1,0,0)), (2,1,(0,1,0)), (3,1,(0,1,1))):
    d = ActionDevice(state, action)
    d.call('app_start_action', action, d.tick)
    for _ in range(6):
        d.advance(400)
        d.call('templogpro_app_process')
    check((d.starts,d.stops,d.formats) == expected)
    check(d.get('g_action_active') == 0)
    d.advance(400)
    d.call('templogpro_app_process')
    check((d.starts,d.stops,d.formats) == expected)

# Weak battery (no USB): CLR and START are refused before any flash work,
# the state stays as it was and nothing is marked in the backup register.
for action, state in ((1,0), (3,1), (3,2)):
    d = ActionDevice(state, action)
    d.vdd_mv = 2550
    d.call('app_start_action', action, d.tick)
    for _ in range(6):
        d.advance(400)
        d.call('templogpro_app_process')
    check((d.starts, d.stops, d.formats) == (0,0,0) and d.flash_ops == [])
    check(d.get('g_app_state') == state and d.get('g_lcd_message_icons') != 0)
for action, state, op in ((1,0,2), (3,2,1)):
    d = ActionDevice(state, action)
    d.call('app_start_action', action, d.tick)
    for _ in range(6):
        d.advance(400)
        d.call('templogpro_app_process')
    check(d.flash_ops == [op, 0])

d = ActionDevice(1,3)
d.call('app_start_action',3,d.tick)
d.held = 1  # Releasing only one CLEAR button also cancels.
d.call('templogpro_app_process')
check(d.get('g_action_active') == 0 and d.formats == 0)
d = ActionDevice(0,1)
d.call('app_start_action',1,d.tick)
d.set('g_app_state',6)
d.call('templogpro_app_process')
check(d.get('g_app_state') == 6 and d.starts == 0)
check(d.call('templogpro_app_start_logging') == 0)
d = ActionDevice(1,3)
d.addresses[symbols['templogpro_logger_stop']] = lambda a,b,c: 0
d.call('app_start_action',3,d.tick)
for _ in range(6):
    d.advance(400)
    d.call('templogpro_app_process')
check(d.get('g_app_state') == 6 and d.formats == 0)

d = Display()
d.set('g_next_measurement_timestamp',1000)
for name in ('g_config','g_app_config'):
    d.uc.mem_write(symbols[name]+32,struct.pack('<I',2053))  # home, MIN, CLEAR
for event, expected in ((2,(2,11,0)), (1,(11,2,0))):
    d.set('g_lcd_view',0)
    d.addresses[symbols['templogpro_buttons_poll']] = lambda a,b,c: event
    for view in expected:
        d.call('templogpro_app_process')
        check(d.get('g_lcd_view') == view)
for mask in (1,5,4095):
    d.uc.mem_write(ARG, f'lcd_menu_mask={mask}\n'.encode())
    check(d.call('templogpro_config_write_text',ARG,len(f'lcd_menu_mask={mask}\n')) == 1)
    check(d.call('templogpro_config_init') == 1)
    d.call('templogpro_config_get',OUT)
    check(struct.unpack('<I',d.uc.mem_read(OUT+32,4))[0] == mask)
for mask in (0,2,4096,8193):
    text=f'lcd_menu_mask={mask}\n'.encode()
    d.uc.mem_write(ARG,text)
    writes=d.flash_writes
    check(d.call('templogpro_config_write_text',ARG,len(text)) == 0)
    check(d.flash_writes == writes)

# Real HTML export must carry logger capacity without truncating its JSON.
d = Device()
d.config()
def info(ptr,b,c):
    values=[0]*14
    values[4],values[13]=1405,42284
    d.uc.mem_write(ptr,struct.pack('<14I',*values))
    return 1
d.addresses[symbols['templogpro_logger_get_info']]=info
prefix=bytes(d.uc.mem_read(symbols['HTML_PREFIX'],120000)).split(b'\0')[0]
offset=len(prefix)+d.call('templogpro_csv_get_size')
check(d.call('templogpro_html_read',offset,OUT,448)==448)
text=bytes(d.uc.mem_read(OUT,448)).decode()
cfg=json.loads(text.split('application/json">')[1].split('</script>')[0])
check(cfg['lcdMenu']==4095 and cfg['capacity']==42284 and cfg['records']==1405)
check(cfg['tzName']=='' and cfg['tzStd']==0 and cfg['tzDst']==0 and cfg['tzStart']==[0,0,0,0])

print(f'PASS action cancellation / menu / capacity: {checks} checks')
