"""The browser demo against the plugin's harness: the same clip, the same raster,
the same settings, the same audio, two renderers.

    python3 demo/tools/crosscheck.py --wftest build/wftest [--keep DIR]

Needs a GPU, Google Chrome, numpy, a built wftest, and stoatworks-backend's
`release/cdpshot.py` (CDPSHOT=path overrides ~/Projects/infrastructure/
stoatworks-backend/release/cdpshot.py). Not run by tools/verify.sh.
(mirrorball's crosscheck, with woofer's clock and audio.)

Each case is rendered twice:

  - by the PAGE: demo/ served from here on a free port and loaded in headless
    Chrome on the real GPU (ANGLE on Metal) at ?size=WxH&clip=C. The page is
    paused, the case's settings applied, its audio chosen, and Restart pressed:
    a new instance at time 0, primed. Then Step is pressed N times, one frame
    of 1/60 s each, so the page has fed 800 N samples of the audio at 48 kHz
    and its clock reads N/60. Mix is set to 0 (the plugin hands the clip
    through untouched, and nothing is fed while paused, so the cone stands
    still), the canvas is read back as the input frame, Mix is put back and the
    page's output is read back. preserveDrawingBuffer is forced on from outside
    so the canvas can be read after its frame; nothing in the page is changed
    for this.
  - by the PLUGIN: `wftest --pipe --fps 60 --size WxH --set ...` (the real
    WooferPlugin in a GL 4.1 context, its clock preset to seconds), fed that
    input frame for every frame, with the same host values: every control as
    the page's own hostValue() sends it, the float32 printed to 9 digits. wftest
    feeds frame k the samples up to (k + 1) x 800 before drawing it, so its
    frame N-1 has had the same 800 N samples as the page; its last frame is
    compared. On the host-FFT case, where the fallback counts samples off the
    host clock instead, the page's frame at N/60 is compared with wftest's
    frame N (its clock at N/60, the same N steps of 1/60).

That host-FFT case is printed, not judged, and it does not agree. wftest
presets the plugin's clock to seconds (SetClockScaleForTest); the page lets
the plugin vote, as a host would, and primes each new instance with five
frames about a millisecond apart so the vote settles before the first frame
shown. Until it settles the plugin runs on its own wall clock, and on the
fallback that clock is what counts the samples: the page's instance takes
about 6 ms (some 190 samples at 32 kHz) of the kick spectrum wftest never
gives it, the fallback's sines are out of phase with wftest's from there, and
the cone takes a different path. The tap-sample counts printed show it. On the
feed() path the clock counts nothing (every fed sample is integrated once),
and the pictures agree.

The two outputs are compared byte for byte (RGB, and alpha separately), and the
coil's position read off both (the page's status accessor, wftest's stderr
summary is not per frame, so only the page's is printed). There is no pass
mark the plugin's own checks give for this; the script prints the difference
and fails only past max 8 codes or 1% of values off. A negative control --
wftest with Drive one slider step (0.001) from the page's on the defaults
case -- must differ from the page by more than the defaults case does, so the
comparison is shown to see a slip that small.

Exit 1 when a feed() case is past that mark, the negative control passes, or
the page logs an error or a WebGL warning.
"""
import argparse
import base64
import functools
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, ".."))
sys.dont_write_bytecode = True

CDPSHOT = os.environ.get("CDPSHOT", os.path.expanduser("~/Projects/infrastructure/stoatworks-backend/release/cdpshot.py"))
sys.path.insert(0, os.path.dirname(CDPSHOT))
import cdpshot  # noqa: E402

# The real GPU: SwiftShader rounds differently and is slow; --disable-gpu has no WebGL.
cdpshot.GL_FLAGS = ["--use-gl=angle", "--use-angle=metal", "--ignore-gpu-blocklist"]

SIZE = "640x360"

# name, clip, audio (the host row's id), frames N, settings by the plugin's own
# names (the page's kitValues(): {'units': x} is in the plugin's units and goes
# through its compiled ParamFrom*, an option is its element's name, a bare
# number the host's 0..1).
CASES = [
    ("defaults, 5.5 s into the track", "scene", "track", 330, {}),
    ("10 cm full-range at +6 dB", "bars", "track", 180, {"Driver": "10 cm Full-range", "Drive": {"units": 6}}),
    ("Height view", "grid", "track", 240, {"View": "Height"}),
    ("open baffle, Ripple 1000, left only", "detail", "left", 200,
     {"Box Qtc": {"units": 0.3}, "Ripple": {"units": 1000}, "Channel": "Left"}),
    ("18 in PA sub at +30 dB, no blur, Mix 50%", "spot", "track", 150,
     {"Driver": "18 in PA Sub", "Drive": {"units": 30}, "Exposure": {"units": 0}, "Mix": 0.5}),
    ("None (host FFT), defaults", "scene", "hostfft", 120, {}),
]

PRESERVE = """
(() => {
  const real = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (kind, attrs) {
    if (kind === 'webgl2') attrs = Object.assign({}, attrs || {}, { preserveDrawingBuffer: true });
    return real.call(this, kind, attrs);
  };
})();
"""

# Every control on the panel, as the page sends it to the instance.
HOST = """import('./plugin.js').then(m => {
  const out = [];
  for (const p of m.PARAMS) {
    const h = m.hostValue(p.ff, m.mounted.params.get(p.id));
    if (h !== null) out.push([p.ff.name, h]);
  }
  return out;
})"""

# Restart with the audio chosen: the host row's own path (a new instance at time 0).
CHOOSE = """import('./plugin.js').then(m => { m.audio.id = %s; m.restartClock(); })"""

STEP = """(async () => {
  const step = [...document.querySelectorAll('.transport .btn')].find((b) => b.textContent === 'Step');
  for (let i = 0; i < %d; i += 1) {
    step.click();
    await new Promise((resolve) => requestAnimationFrame(() => resolve()));
  }
  const m = await import('./plugin.js');
  return { time: m.mounted.state.time, stats: m.renderer.stats() };
})()"""


class Page(cdpshot.Chrome):
    """cdpshot's Chrome, keeping the events it would discard."""

    def __init__(self):
        self.events = []
        super().__init__()
        self.call("Runtime.enable")
        self.call("Log.enable")
        self.call("Page.addScriptToEvaluateOnNewDocument", {"source": PRESERVE})

    def call(self, method, params=None):
        self.msg_id += 1
        want = self.msg_id
        self.ws.send(json.dumps({"id": want, "method": method, "params": params or {}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == want:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)

    def run(self, expression):
        r = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        if "exceptionDetails" in r:
            raise RuntimeError(f"the page threw: {json.dumps(r['exceptionDetails'])[:400]}")
        return r.get("result", {}).get("value")

    def frames(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.frames || 0)"))

    def instances(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.instances || 0)"))

    def load(self, url):
        self.call("Page.navigate", {"url": url})
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                if self.frames() > 2:
                    return
            except RuntimeError:
                pass
            time.sleep(0.25)
        raise RuntimeError(f"{url} drew no frames")

    def after(self, expression):
        """Run, then wait for the frame it causes (the page is paused)."""
        n0 = self.run("Number(document.querySelector('canvas')?.dataset.frames || 0) + ':' + (document.querySelector('canvas')?.dataset.instances || 0)")
        self.run(expression)
        deadline = time.time() + 20
        while time.time() < deadline:
            now = self.run("Number(document.querySelector('canvas')?.dataset.frames || 0) + ':' + (document.querySelector('canvas')?.dataset.instances || 0)")
            if now != n0:
                return
            time.sleep(0.03)
        raise RuntimeError("no frame after " + expression[:80])

    def canvas(self):
        """The canvas's bytes, rows top first."""
        r = self.run("""(() => {
          const c = document.querySelector('canvas'); const gl = c.getContext('webgl2');
          const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
          const out = new Uint8Array(w * h * 4);
          for (let y = 0; y < h; y += 1) out.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
          let s = '';
          for (let i = 0; i < out.length; i += 0x8000) s += String.fromCharCode.apply(null, out.subarray(i, i + 0x8000));
          return { w, h, b64: btoa(s) };
        })()""")
        return r["w"], r["h"], base64.b64decode(r["b64"])

    def problems(self):
        out = []
        for e in self.events:
            m, p = e["method"], e.get("params", {})
            if m == "Runtime.exceptionThrown":
                out.append(json.dumps(p.get("exceptionDetails", {}))[:300])
            elif m == "Runtime.consoleAPICalled" and p.get("type") in ("error", "warning", "assert"):
                out.append(" ".join(str(a.get("value", a.get("description", ""))) for a in p.get("args", []))[:300])
            elif m == "Log.entryAdded" and p.get("entry", {}).get("level") in ("error", "warning"):
                out.append(f"{p['entry'].get('source')}: {p['entry'].get('text')}"[:300])
        return out


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    handler = functools.partial(Quiet, directory=DEMO)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def wftest(binary, host, frame, count, audio, override=None):
    """The last of `count` frames, each fed the same input frame."""
    sets = []
    for name, value in host:
        value = (override or {}).get(name, value)
        sets += ["--set", f"{name}={value:.9g}" if isinstance(value, float) else f"{name}={value}"]
    extra = {"track": [], "left": ["--left-only"], "silence": ["--silent"], "hostfft": ["--host-fft"]}[audio]
    proc = subprocess.Popen([binary, "--pipe", "--fps", "60", "--size", SIZE, *extra, *sets],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def feed():
        try:
            for _ in range(count):
                proc.stdin.write(frame)
            proc.stdin.close()
        except BrokenPipeError:
            pass

    writer = threading.Thread(target=feed, daemon=True)
    writer.start()
    size = len(frame)
    last, got = None, 0
    while True:
        chunk = proc.stdout.read(size)
        if not chunk:
            break
        if len(chunk) < size:
            chunk += proc.stdout.read(size - len(chunk))
        if len(chunk) == size:
            last, got = chunk, got + 1
    writer.join()
    err = proc.stderr.read().decode()
    if proc.wait() != 0 or got != count:
        raise RuntimeError(f"wftest {' '.join(extra + sets)}: {got} of {count} frames; {err[:400]}")
    return last, err.strip().splitlines()[-1] if err.strip() else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--wftest", required=True)
    ap.add_argument("--keep", help="write each case's two pictures and their difference (x40) here as PNG")
    ap.add_argument("--only", help="run only the cases whose name contains this")
    args = ap.parse_args()

    w, h = map(int, SIZE.split("x"))
    server, base = serve()
    page = Page()
    failures = 0
    try:
        for name, clip, audio, count, settings in CASES:
            if args.only and args.only not in name:
                continue
            page.load(f"{base}?size={SIZE}&clip={clip}")
            page.after("document.querySelector('.btn--play').click()")
            if page.run("import('./plugin.js').then(m => m.mounted.state.playing)"):
                raise RuntimeError("the page did not pause")
            kit = page.run(f"import('./plugin.js').then(m => m.kitValues({json.dumps(settings)}))")
            if kit:
                page.after(f"import('./plugin.js').then(m => m.mounted.params.apply({json.dumps(kit)}))")
            before = page.instances()
            page.after(CHOOSE % json.dumps(audio))
            if page.instances() != before + 1:
                raise RuntimeError("choosing the audio did not start a new instance")
            stepped = page.run(STEP % count)
            if abs(stepped["time"] - count / 60) > 1e-9:
                raise RuntimeError(f"the page's clock reads {stepped['time']} after {count} steps")
            mix = page.run("import('./plugin.js').then(m => m.mounted.params.get('mix'))")
            page.after("import('./plugin.js').then(m => m.mounted.params.set('mix', 0))")
            cw, ch, frame = page.canvas()
            if (cw, ch) != (w, h):
                raise RuntimeError(f"the canvas is {cw}x{ch}, not {SIZE}")
            page.after(f"import('./plugin.js').then(m => m.mounted.params.set('mix', {mix}))")
            cw, ch, ours = page.canvas()
            host = page.run(HOST)
            frames = count + 1 if audio == "hostfft" else count
            theirs, summary = wftest(args.wftest, host, frame, frames, audio)

            a = np.frombuffer(ours, np.uint8).reshape(h, w, 4).astype(np.int16)
            b = np.frombuffer(theirs, np.uint8).reshape(h, w, 4).astype(np.int16)
            diff = np.abs(a - b)
            rgb = diff[..., :3]
            off = int((rgb > 0).sum())
            ok = rgb.max() <= 8 and off <= 0.01 * rgb.size and diff[..., 3].max() <= 1
            # The host-FFT case is reported, not judged: see the docstring.
            informational = audio == "hostfft"
            failures += not ok and not informational
            s = stepped["stats"]
            mark = "info" if informational else "ok  " if ok else "FAIL"
            print(f"{mark}  {name:<42} {clip:<6} max {rgb.max()} codes, {off} of {rgb.size} values off"
                  f" (mean {rgb.mean():.2e}), alpha max {diff[..., 3].max()}"
                  f" | page: x {s['x'] * 1000:+.4f} mm, {s['hits']:.0f} stop hits, {s['count']:.0f} tap samples, earshot {s['state']} | wftest: {summary}")
            if name.startswith("defaults"):
                defaults_mean = rgb.mean()
                step = dict(host)["Drive"] + 0.001
                slip_bytes, _ = wftest(args.wftest, host, frame, frames, audio, {"Drive": step})
                slip = np.abs(a - np.frombuffer(slip_bytes, np.uint8).reshape(h, w, 4).astype(np.int16))[..., :3]
                caught = slip.mean() > defaults_mean
                failures += not caught
                print(f"{'ok  ' if caught else 'FAIL'}  {'negative: Drive one step (0.001) off':<42} {'':<6} max {slip.max()} codes, mean {slip.mean():.2e}"
                      f" against the defaults' {defaults_mean:.2e}, {int((slip > 0).sum())} values off")
            if args.keep:
                from PIL import Image
                os.makedirs(args.keep, exist_ok=True)
                stem = os.path.join(args.keep, "".join(c if c.isalnum() else "-" for c in name))
                Image.fromarray(a[..., :3].astype(np.uint8)).save(f"{stem}-page.png")
                Image.fromarray(b[..., :3].astype(np.uint8)).save(f"{stem}-wftest.png")
                Image.fromarray(np.clip(rgb * 40, 0, 255).astype(np.uint8)).save(f"{stem}-diff-x40.png")
        problems = page.problems()
        for p in problems:
            print(f"FAIL  the page logged: {p}")
        failures += len(problems)
        renderer = page.run("(() => { const gl = document.createElement('canvas').getContext('webgl2'); const x = gl.getExtension('WEBGL_debug_renderer_info'); return x ? gl.getParameter(x.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER); })()")
        print(f"\nthe page rendered on: {renderer}")
    finally:
        page.close()
        server.shutdown()
    print(f"{'all cases within the mark' if failures == 0 else f'{failures} failure(s)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", tempfile.gettempdir())
    sys.exit(main())
