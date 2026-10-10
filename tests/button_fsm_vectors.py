#!/usr/bin/env python3
"""Vector tests for the TempLogPro two-button state machine.

The firmware implementation lives in Src/templogpro_buttons.c. This script keeps
the same public events and timing constants so edge cases can be checked without
flashing the MCU.
"""

BUTTON_NONE = "NONE"
BUTTON_START_SHORT = "START_SHORT"
BUTTON_STOP_SHORT = "STOP_SHORT"
BUTTON_START_LONG_CONFIRMED = "START_LONG_CONFIRMED"
BUTTON_STOP_LONG_CONFIRMED = "STOP_LONG_CONFIRMED"
BUTTON_BOTH_LONG_CONFIRMED = "BOTH_LONG_CONFIRMED"
BUTTON_PRESS_CANCELLED = "PRESS_CANCELLED"

MASK_START = 0x01
MASK_STOP = 0x02
MASK_BOTH = MASK_START | MASK_STOP

BUTTON_DEBOUNCE_MS = 50
BUTTON_SHORT_MAX_MS = 700
BUTTON_LONG_MS = 3000


class ButtonFsm:
    IDLE = 0
    TRACK_PRESS = 1
    WAIT_FULL_RELEASE = 2

    def __init__(self):
        self.raw_mask = 0
        self.candidate_mask = 0
        self.candidate_since_ms = 0
        self.stable_mask = 0
        self.press_mask = 0
        self.combined_mask = 0
        self.press_start_ms = 0
        self.both_start_ms = 0
        self.state = self.IDLE

    def update_stable(self, raw_mask, now_ms):
        raw_mask &= MASK_BOTH
        self.raw_mask = raw_mask
        if raw_mask != self.candidate_mask:
            self.candidate_mask = raw_mask
            self.candidate_since_ms = now_ms
            return
        if (self.stable_mask != self.candidate_mask and
                now_ms - self.candidate_since_ms >= BUTTON_DEBOUNCE_MS):
            self.stable_mask = self.candidate_mask

    def reset_press_tracking(self):
        self.press_mask = 0
        self.combined_mask = 0
        self.press_start_ms = 0
        self.both_start_ms = 0

    @staticmethod
    def short_event(mask):
        if mask == MASK_START:
            return BUTTON_START_SHORT
        if mask == MASK_STOP:
            return BUTTON_STOP_SHORT
        return BUTTON_NONE

    @staticmethod
    def long_event(mask):
        if mask == MASK_START:
            return BUTTON_START_LONG_CONFIRMED
        if mask == MASK_STOP:
            return BUTTON_STOP_LONG_CONFIRMED
        if mask == MASK_BOTH:
            return BUTTON_BOTH_LONG_CONFIRMED
        return BUTTON_NONE

    def poll(self, raw_mask, now_ms):
        self.update_stable(raw_mask, now_ms)

        if self.state == self.WAIT_FULL_RELEASE:
            if self.stable_mask == 0:
                self.reset_press_tracking()
                self.state = self.IDLE
            return BUTTON_NONE

        if self.state == self.IDLE:
            if self.stable_mask != 0:
                self.press_mask = self.stable_mask
                self.combined_mask = self.stable_mask
                self.press_start_ms = now_ms
                self.both_start_ms = now_ms if self.stable_mask == MASK_BOTH else 0
                self.state = self.TRACK_PRESS
            return BUTTON_NONE

        if self.stable_mask != 0:
            self.combined_mask |= self.stable_mask
            if self.stable_mask == MASK_BOTH and self.both_start_ms == 0:
                self.both_start_ms = now_ms
            if (self.stable_mask == MASK_BOTH and self.both_start_ms != 0 and
                    now_ms - self.both_start_ms >= BUTTON_LONG_MS):
                self.state = self.WAIT_FULL_RELEASE
                return BUTTON_BOTH_LONG_CONFIRMED
            if (self.combined_mask == self.press_mask and
                    self.stable_mask == self.press_mask and
                    now_ms - self.press_start_ms >= BUTTON_LONG_MS):
                self.state = self.WAIT_FULL_RELEASE
                return self.long_event(self.press_mask)
            return BUTTON_NONE

        if (now_ms - self.press_start_ms <= BUTTON_SHORT_MAX_MS and
                self.combined_mask == self.press_mask):
            event = self.short_event(self.press_mask)
        else:
            event = BUTTON_PRESS_CANCELLED
        self.state = self.WAIT_FULL_RELEASE
        return event


def run_sequence(points):
    fsm = ButtonFsm()
    events = []
    for now_ms, raw_mask in points:
        event = fsm.poll(raw_mask, now_ms)
        if event != BUTTON_NONE:
            events.append((now_ms, event))
    return events


def make_press(mask, press_ms, release_tail_ms=80):
    points = [(0, 0), (1, mask)]
    for t in range(0, press_ms + release_tail_ms + 1, 10):
        raw = mask if t < press_ms else 0
        points.append((t, raw))
    points.append((press_ms + BUTTON_DEBOUNCE_MS, 0))
    points.append((press_ms + release_tail_ms, 0))
    return sorted(set(points))


def expect(name, points, expected):
    events = [event for _, event in run_sequence(points)]
    if events != expected:
        raise AssertionError(f"{name}: expected {expected}, got {events}")


def test_short_pulses():
    expect("10 ms ignored", make_press(MASK_START, 10), [])
    expect("49 ms ignored", make_press(MASK_START, 49), [])
    expect("80 ms START short", make_press(MASK_START, 80), [BUTTON_START_SHORT])
    expect("100 ms STOP short", make_press(MASK_STOP, 100), [BUTTON_STOP_SHORT])


def test_bounce_press_and_release():
    points = [
        (0, 0), (1, MASK_START), (10, 0), (20, MASK_START), (30, 0),
        (40, MASK_START), (90, MASK_START), (140, MASK_START),
        (160, 0), (170, MASK_START), (180, 0), (190, MASK_START),
        (200, 0), (250, 0), (300, 0),
    ]
    expect("bounce still one short", points, [BUTTON_START_SHORT])


def test_long_edges():
    expect("2.9 s no long", make_press(MASK_START, 2900), [BUTTON_PRESS_CANCELLED])
    expect("3.1 s START long once", make_press(MASK_START, 3150), [BUTTON_START_LONG_CONFIRMED])
    expect("10 s STOP long once", make_press(MASK_STOP, 10000), [BUTTON_STOP_LONG_CONFIRMED])


def test_combinations():
    points = [(0, 0), (1, MASK_START), (60, MASK_START), (1000, MASK_BOTH)]
    points += [(t, MASK_BOTH) for t in range(1050, 4201, 250)]
    points += [(4250, 0), (4300, 0)]
    expect("START then STOP becomes both long", points, [BUTTON_BOTH_LONG_CONFIRMED])

    points = [(0, 0), (1, MASK_BOTH)]
    points += [(t, MASK_BOTH) for t in range(60, 3201, 250)]
    points += [(3250, 0), (3300, 0)]
    expect("near simultaneous both", points, [BUTTON_BOTH_LONG_CONFIRMED])

    points = [(0, 0), (1, MASK_START), (60, MASK_START), (500, MASK_BOTH),
              (560, MASK_BOTH), (650, MASK_START), (720, 0), (780, 0)]
    expect("mixed short cancelled", points, [BUTTON_PRESS_CANCELLED])


def test_sleep_and_busy_policy_vectors():
    expect("screen sleep short wake event source", make_press(MASK_START, 90), [BUTTON_START_SHORT])
    expect("dark screen long start event source", make_press(MASK_START, 3200), [BUTTON_START_LONG_CONFIRMED])
    expect("during Winbond write same FSM", make_press(MASK_STOP, 90), [BUTTON_STOP_SHORT])
    expect("during USB same FSM", make_press(MASK_STOP, 3200), [BUTTON_STOP_LONG_CONFIRMED])


def main():
    test_short_pulses()
    test_bounce_press_and_release()
    test_long_edges()
    test_combinations()
    test_sleep_and_busy_policy_vectors()
    print("button_fsm_vectors: OK")


if __name__ == "__main__":
    main()
