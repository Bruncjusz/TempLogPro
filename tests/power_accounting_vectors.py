#!/usr/bin/env python3
"""Vector tests for software power telemetry accounting."""


UINT32_MAX = 0xFFFFFFFF
VDD_CACHE_MS = 5000


def add_u32(value, increment):
    return min(UINT32_MAX, value + increment)


def duty_permille(active_ms, uptime_ms):
    if uptime_ms == 0:
        return 0
    return min(1000, (active_ms * 1000) // uptime_ms)


class PowerAccounting:
    def __init__(self, now_ms=0, usb=False):
        self.last_ms = now_ms
        self.last_usb = usb
        self.usb_ms = 0
        self.battery_ms = 0
        self.vdd_last_ms = 0
        self.vdd_valid = False
        self.vdd_samples = 0

    def process(self, now_ms, usb):
        elapsed = (now_ms - self.last_ms) & UINT32_MAX
        if self.last_usb:
            self.usb_ms = add_u32(self.usb_ms, elapsed)
        else:
            self.battery_ms = add_u32(self.battery_ms, elapsed)
        self.last_ms = now_ms
        self.last_usb = usb

    def sample_vdd(self, now_ms):
        if self.vdd_valid and ((now_ms - self.vdd_last_ms) & UINT32_MAX) < VDD_CACHE_MS:
            return False
        self.vdd_valid = True
        self.vdd_last_ms = now_ms
        self.vdd_samples += 1
        return True


def expect(name, actual, expected):
    if actual != expected:
        raise AssertionError(f"{name}: expected {expected}, got {actual}")


def main():
    stats = PowerAccounting(now_ms=0, usb=False)
    stats.process(100, False)
    stats.process(150, True)
    stats.process(225, True)
    expect("battery time before source transition", stats.battery_ms, 150)
    expect("USB time after source transition", stats.usb_ms, 75)

    expect("saturating counter", add_u32(UINT32_MAX - 2, 10), UINT32_MAX)
    expect("zero uptime duty", duty_permille(10, 0), 0)
    expect("25 percent duty", duty_permille(250, 1000), 250)
    expect("duty clamp", duty_permille(1100, 1000), 1000)

    expect("first VDD conversion", stats.sample_vdd(1000), True)
    expect("cached VDD conversion", stats.sample_vdd(5999), False)
    expect("VDD conversion after cache", stats.sample_vdd(6000), True)
    expect("VDD sample count", stats.vdd_samples, 2)

    print("power_accounting_vectors: OK")


if __name__ == "__main__":
    main()
