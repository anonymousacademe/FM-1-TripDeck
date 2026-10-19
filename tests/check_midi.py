#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
r"""Check the trace of tests/midi.txt: the TRS jack's MIDI in (running status, real-time bytes inside messages, SysEx
   ignored, velocity 0, program change, pitch wheel, all notes off) and the notes that go out over USB-MIDI.
   .\fm1.ps1 host tripdeck tests/midi.txt > build\host\midi-trace.txt ; python tests\check_midi.py build\host\midi-trace.txt"""
import re, sys

on, off, out, preset, bend = [], [], [], [], []
for line in open(sys.argv[1]):
    m = re.match(r"\s*(\d+) ms  voice on\s+(\d+) vel\s+(\d+)", line)
    if m: on.append((int(m[1]), int(m[2]), int(m[3]))); continue
    m = re.match(r"\s*(\d+) ms  voice off\s+(\d+)", line)
    if m: off.append((int(m[1]), int(m[2]))); continue
    m = re.match(r"\s*(\d+) ms  MIDI out ([0-9A-F]+) (\d+) (\d+)", line)
    if m: out.append((int(m[1]), int(m[2], 16), int(m[3]), int(m[4]))); continue
    m = re.match(r"\s*(\d+) ms  preset index (\d+)", line)
    if m: preset.append((int(m[1]), int(m[2]))); continue
    m = re.match(r"\s*(\d+) ms  pitch bend (-?\d+)", line)
    if m: bend.append((int(m[1]), int(m[2])))


def voices(a, b, kind=on):
    return sorted({e[1] for e in kind if a <= e[0] < b})


outs_on = [(t, n, v) for t, s, n, v in out if s == 0x90]
outs_off = [(t, n) for t, s, n, v in out if s == 0x80]
checks = [
    ("A: running status plays C4 E4 G4", voices(100, 300) == [60, 64, 67] and all(v == 100 for t, n, v in on if t < 300)),
    ("B: note offs with clock bytes inside the messages", voices(600, 800, off) == [60, 64, 67]),
    ("C: SysEx ignored, C5 plays at velocity 80", any(n == 72 and v == 80 and 900 <= t < 1000 for t, n, v in on)),
    ("C: note on with velocity 0 is a note off", any(n == 72 and 1100 <= t < 1200 for t, n in off)),
    ("D: program change selects index 3", any(n == 3 and 1300 <= t < 1400 for t, n in preset)),
    ("E: pitch wheel up (+8191) then centred (0)", [b for t, b in bend if 1500 <= t < 1700] == [8191, 0]),
    ("F: all notes off goes out over USB (CC 123)", any(s == 0xB0 and n == 123 and 2100 <= t < 2200 for t, s, n, v in out)),
    ("G: the arp's notes go out (C4 E4 G4 only), six or more", len([1 for t, n, v in outs_on if 2500 <= t < 4400]) >= 6
        and {n for t, n, v in outs_on if 2500 <= t < 4400} == {60, 64, 67}),
    ("G: and each is ended", abs(len([1 for t, n in outs_off if 2500 <= t < 4500]) - len([1 for t, n, v in outs_on if 2500 <= t < 4400])) <= 1),
    ("H: a panel key with the arp off goes out directly", any(n == 53 and v == 100 and 5000 <= t < 5100 for t, n, v in outs_on)),
    ("I: the sequencer's first note (A2, accent) goes out", any(n == 45 and v == 127 and 5600 <= t < 5800 for t, n, v in outs_on)),
    ("I: and the sequencer notes are ended", len([1 for t, n in outs_off if 5600 <= t < 7200]) >= 3),
]
bad = 0
for name, ok in checks:
    print(("ok    " if ok else "FAIL  ") + name)
    bad += not ok
print(f"{len(checks) - bad} of {len(checks)} ok")
sys.exit(1 if bad else 0)
