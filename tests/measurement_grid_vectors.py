"""Measurement schedule on the actual Release ARM image.

The main loop is woken late by a random 0-4 s (STOP wake-up, USB work, flash
erase). Measurements must stay on the 10 s grid set at START instead of
drifting by the accumulated lateness, and missed slots are skipped.
"""
import random
from usb_modes_vectors import Device, check

INTERVAL = 10
START_TS = 1000
DURATION = 3600

d = Device()
d.config(report=False, usb=True)
check(d.call('templogpro_sensor_init') == 1)
d.ts = START_TS
d.set('g_next_measurement_timestamp', 0)

request_times = []


def process():
    pending_before = d.call('templogpro_sensor_pending')
    d.call('templogpro_app_process')
    if not pending_before and d.call('templogpro_sensor_pending'):
        request_times.append(d.ts)
    d.advance(30)
    d.call('templogpro_app_process')   # conversion done, sample stored


rng = random.Random(20261009)
while d.ts < START_TS + DURATION:
    process()
    d.ts += 1 + (rng.randint(0, 4) if rng.random() < 0.3 else 0)
    d.advance(1000)

offsets = [(t - START_TS) % INTERVAL for t in request_times]
check(request_times[0] == START_TS)
check(max(offsets) <= 4)                       # never more than the injected lateness
check(len(request_times) >= DURATION // INTERVAL - 1)
check(len(d.samples) >= len(request_times) - 1)
check(all(b - a >= INTERVAL - 4 for a, b in zip(request_times, request_times[1:])))
print(f'PASS measurement grid: {len(request_times)} measurements in {DURATION} s, '
      f'max offset {max(offsets)} s from the {INTERVAL} s grid')
