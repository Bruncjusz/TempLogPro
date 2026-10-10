#!/usr/bin/env python3
"""Vector tests for the RTC monotonic timestamp guard."""


class MonotonicRtc:
    def __init__(self, raw_timestamp):
        self.raw = raw_timestamp
        self.last_raw = 0
        self.offset = 0
        self.floor = 0
        self.guard_events = 0

    def get(self):
        if self.last_raw and self.raw < self.last_raw:
            self.offset += self.last_raw - self.raw
            self.guard_events += 1
        corrected = self.raw + self.offset
        if corrected < self.floor:
            self.offset += self.floor - corrected
            corrected = self.floor
            self.guard_events += 1
        self.last_raw = self.raw
        self.floor = max(self.floor, corrected)
        return corrected

    def apply_record_floor(self, timestamp):
        current = self.get()
        if current < timestamp:
            self.offset += timestamp - current
            self.guard_events += 1
        self.floor = max(self.floor, timestamp)

    def set_time(self, timestamp):
        if self.floor and timestamp < self.floor:
            return False
        self.raw = timestamp
        self.last_raw = timestamp
        self.offset = 0
        self.floor = max(self.floor, timestamp)
        return True


def expect(name, actual, expected):
    if actual != expected:
        raise AssertionError(f"{name}: expected {expected}, got {actual}")


def main():
    rtc = MonotonicRtc(900)
    rtc.apply_record_floor(1000)
    expect("startup floor", rtc.get(), 1000)
    expect("one guard event", rtc.guard_events, 1)

    expect("same second stable 1", rtc.get(), 1000)
    expect("same second stable 2", rtc.get(), 1000)
    expect("no artificial acceleration", rtc.guard_events, 1)

    rtc.raw = 910
    expect("normal progress after offset", rtc.get(), 1010)

    rtc.raw = 700
    expect("runtime rollback held", rtc.get(), 1010)
    expect("runtime rollback event", rtc.guard_events, 2)
    rtc.raw = 710
    expect("progress resumes", rtc.get(), 1020)

    expect("backward manual set blocked", rtc.set_time(1019), False)
    expect("equal manual set accepted", rtc.set_time(1020), True)
    expect("forward manual set accepted", rtc.set_time(2000), True)
    expect("forward time visible", rtc.get(), 2000)

    print("rtc_monotonic_vectors: OK")


if __name__ == "__main__":
    main()
