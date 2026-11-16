"""Render every control at two settings and fail if any made no difference.
(ferrofluid's script by way of fountain; the table is woofer's.)

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Every render is 183 frames (3.05 s at 60 fps) at 320x180 (CI's raster), fed
the harness's track (a kick on the beat, a hat on the off-beat, a 55 Hz bass)
through earshot's feed(), as a device would be. 183 and not 180: at 3.00 s a
kick has only just begun, the cone is where the bass note alone puts it, and
Coil Offset and Xmax -- which act only on a big stroke -- measured 0.0000 and
0.0060 there; 50 ms into the kick they measure about 4. Channel needs a stereo
difference, so it is swept with the track on the left only. FFT Law only acts
on the fallback, so it is swept with nothing fed and a synthetic spectrum in
Resolume's FFT buffer (`--host-fft`). Audio Input and Rescan Inputs are covered
by `wftest --devices` on the scripted backend instead: no step here may touch
an audio device. An option parameter reads back 0..1 whatever its count (the
fleet's trap), so options are set here by name. A control whose two renders
differ by less than `--floor` (mean 8-bit difference per channel) is barely
alive: vectrix's lesson. One control has its own floor, with the reason beside
it: the status lamp is an LED a few pixels across, by design.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context, the addendum's honesty check: what
goes dead there is the list of controls that only act with another one set,
which AGENTS.md records.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

BASE = []

# name -> (low settings, high settings, context settings)
SWEEP = {
    "Driver": (["Driver=18 cm Midwoofer"], ["Driver=30 cm Subwoofer"], []),
    "Box Qtc": (["Box Qtc=0"], ["Box Qtc=1"], []),
    "Xmax": (["Xmax=0"], ["Xmax=1"], []),
    "Coil Offset": (["Coil Offset=0"], ["Coil Offset=1"], []),
    "Breakup": (["Breakup=0"], ["Breakup=1"], ["Ripple=1", "Exposure=0"]),
    "Channel": (["Channel=Left"], ["Channel=Right"], [["--left-only"]]),
    "Drive": (["Drive=0.2"], ["Drive=0.8"], []),
    "FFT Law": (["FFT Law=Linear Mag"], ["FFT Law=Log Power"], [["--host-fft"]]),
    "Size": (["Size=0.3"], ["Size=0.7"], []),
    "Camera": (["Camera=0"], ["Camera=1"], []),
    "Exposure": (["Exposure=0"], ["Exposure=1"], []),
    "Ripple": (["Ripple=0"], ["Ripple=1"], ["Exposure=0"]),
    "Light Angle": (["Light Angle=0.2"], ["Light Angle=0.8"], []),
    "Light Turn": (["Light Turn=0.2"], ["Light Turn=0.7"], []),
    "Ambient": (["Ambient=0.1"], ["Ambient=0.9"], []),
    "Gloss": (["Gloss=0"], ["Gloss=1"], []),
    "Status Lamp": (["Status Lamp=Off"], ["Status Lamp=On"], []),
    "View": (["View=Lit"], ["View=Height"], []),
    "Mix": (["Mix=0"], ["Mix=1"], []),
}

# A control whose whole effect is small by design, and the floor it is held to
# instead. The lamp is a disc 0.008 frame heights in radius: at 320x180 about
# 12 pixels of 57600, so turning it off moves the frame's mean by about 0.015
# codes when nothing else changes. Its own pixels change by up to 255.
FLOOR = {"Status Lamp": 0.005}

# The About block: a text line and browser buttons, with no pixel to sweep.
# Audio is Resolume's FFT buffer (written by --host-fft, swept through FFT Law).
# Audio Input and Rescan Inputs: wftest --devices, on the scripted backend.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide", "Audio", "Audio Input", "Rescan Inputs"}


def read_png(path):
    """Enough of PNG for wftest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(wftest, out, settings):
    args = [str(wftest), "--out", str(out), "--size", "320x180", "--frames", "183"]
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"wftest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(wftest):
    result = subprocess.run([str(wftest), "--list"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"wftest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(wftest, scratch, name, bare):
    low, high, context = SWEEP[name]
    # A source flag (--left-only, --host-fft) is what the control acts on, not
    # a context, so --bare keeps it.
    sources = [c for c in context if isinstance(c, list)]
    context = BASE + (sources if bare else context)
    tag = f"p{abs(hash(name))}"
    before = render(wftest, scratch / f"{tag}-a.png", context + low)
    after = render(wftest, scratch / f"{tag}-b.png", context + high)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    wftest = build / "wftest"
    if not wftest.exists():
        print(f"{wftest} not found", file=sys.stderr)
        return 1

    declared = parameters(wftest)
    unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
    if unknown:
        # A new parameter with no sweep is a hole, not a pass.
        print(f"no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
        return 1
    dead, weak = [], []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            names = [n for n in declared if n in SWEEP]
            results = list(pool.map(lambda n: sweep_one(wftest, scratch, n, args.bare), names))
    for name, delta in results:
        if delta == 0.0:
            dead.append(name)
            print(f"  DEAD {name:16s} both ends identical")
        elif delta < FLOOR.get(name, args.floor):
            weak.append(name)
            print(f"  WEAK {name:16s} mean delta {delta:.4f}")
        elif args.verbose:
            print(f"  ok   {name:16s} mean delta {delta:.3f}")
    print(f"SW Woofer: {len(results)} parameters swept{' (bare: sources only)' if args.bare else ''}, "
          f"{len(dead)} dead, {len(weak)} barely alive")
    if dead or weak:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
