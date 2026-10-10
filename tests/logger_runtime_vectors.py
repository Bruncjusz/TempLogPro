#!/usr/bin/env python3
"""Runtime accounting vectors for TempLogPro logger events."""

START = "START"
RESUME = "RESUME"
PAUSE = "PAUSE"
MEAS = "MEAS"


def active_seconds(events, now=None, running=False):
    active = 0
    run_start = None
    first_ts = None
    last_ts = None

    for ts, kind in events:
        if first_ts is None:
            first_ts = ts
        last_ts = ts
        if kind == START:
            run_start = ts
        elif kind == RESUME and run_start is None:
            run_start = ts
        elif kind == PAUSE:
            if run_start is not None and ts >= run_start:
                active += ts - run_start
            run_start = None

    if running and run_start is not None and now is not None and now >= run_start:
        active += now - run_start

    total = 0 if first_ts is None or last_ts is None else last_ts - first_ts
    return active, total


def expect(name, events, expected_active, expected_total, now=None, running=False):
    active, total = active_seconds(events, now=now, running=running)
    if (active, total) != (expected_active, expected_total):
        raise AssertionError(
            f"{name}: expected active/total {(expected_active, expected_total)}, got {(active, total)}"
        )


def main():
    expect("single run", [(100, START), (110, MEAS), (160, PAUSE)], 60, 60)
    expect("pause excluded",
           [(100, START), (130, MEAS), (160, PAUSE), (300, RESUME), (330, MEAS), (350, PAUSE)],
           110, 250)
    expect("running live",
           [(100, START), (160, PAUSE), (300, RESUME), (330, MEAS)],
           160, 230, now=400, running=True)
    expect("resume without start",
           [(300, RESUME), (360, PAUSE)],
           60, 60)
    expect("empty", [], 0, 0)
    print("logger_runtime_vectors: OK")


if __name__ == "__main__":
    main()
