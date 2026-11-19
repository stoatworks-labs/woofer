# AGENTS.md: Woofer

Read this before changing the integrator, Bl(x) or the stop, the profile table and the roll,
the fallback, the device menu or the clock. `CLAUDE.md` has the commands; the spec is
`~/Projects/resolume/specs/SPEC-woofer.md` (its As-built section records where this repo
departs from it).

## Status

**v0.1.0, released 2026-11-18, public at `github.com/stoatworks-labs/woofer`.**
`OXBOW=… tools/verify.sh` passes on this Mac (Apple
Silicon, macOS 26) against a fresh universal Release build: every check at 1280x720 and
320x180, six on Apple's software renderer, `--offline` with no GL context, the pipe, 19 of 19
negative controls, 17 of 17 mutants, 19 of 19 controls alive in the sweep, oxbow reading and
rendering the bundle with devices disabled. **In Resolume only briefly** (2026-11-18): Arena
7.29.0 on this Mac over REST (registers, 28 parameters, Mix 0 is the still; Audio Input is a
value, not an index; Resolume's Composition FFT moves the cone with `Beat 001.wav` playing below:
peaks of 5.6% of Xmech at +18 dB Linear Mag, 15.1% at +30 dB, 69% under Log Power, re-tested
2026-11-19; the 2026-11-18 run heard nothing because `Beat 001.mov` has no audio stream) and Arena 7.27.1 on win-lab (the fleet's gate: 9 passed,
0 failed, 2 skipped; `plugin-bench/arena/expect/woofer.json`). README "In Resolume" has the
detail. **No live audio device ever opened**, by woofer or by earshot.
earshot is a snapshot of `github.com/stoatworks-labs/earshot` at `8124191`, unpatched.

## The idea

**The clip is printed on the front of a loudspeaker, and the music drives the loudspeaker.**
A real driver (Thiele–Small parameters from five datasheets) in a sealed box is voltage-driven
by the audio waveform at 32 kHz: Le di/dt = V − Re i − Bl(x) v; Mms dv/dt = Bl(x) i − Rms v −
Fsus(x) + Fbox(x). The stroke moves the clip, printed on the driver at rest from the camera,
toward and away from a pinhole; the surround's roll bends at constant arc length; the front is
lit as a heightfield. Audio drives the mechanism, never a parameter wobble.

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Woofer`, `WF01`, effect, `com.stoatworks.ffgl.woofer`, harness `wftest`.
  One plugin, the About block last.
- **earshot, snapshotted, not a submodule.** `external/earshot/` is `git archive 8124191` of
  earshot (`github.com/stoatworks-labs/earshot`), with `SNAPSHOT` saying so and "Patches: none". Configured
  with `tapRate = 32000`, `maxChannels = 2`, four bands with edges {20, 200, 800, 3200, 12800}
  Hz (used only by the fallback: under the Linear law bin 0, centred on 172 Hz, lands in the
  first band; earshot's default 30–150 Hz band gets no bin at all).
- **The integrator.** Classical RK4, two substeps per tap sample (h = 1/64000, planned from the
  10F's electrical pole at Re/Le, |λh| = 0.9 against RK4's 2.78), the voltage between samples
  by Catmull–Rom, **one sample behind**: trace index k pairs with volts index k − 1, and every
  check that compares x with V shifts by one (`dftAt(..., shift = −1)`). Breakup modes are
  discretised exactly over each sample (matrix exponential, the sample's mean acceleration
  held). Each sample's state, after its two substeps, goes into an 8192-sample ring (0.256 s;
  the spec said every substep: the stop and max |x| are still checked at every substep, but
  the picture needs no more than 32 kHz); the picture reads the ring at audio-time instants,
  so frames never enter the physics (`--framerate`: bit-identical at 24/30/60/144 fps).
- **Bl(x)** is the closed-form overlap of the coil window with a trapezoidal gap field (flat
  for |z| ≤ Hg/4, ramps Hg/2 wide), normalised to Bl0 at the coil's rest. Its plateau is
  |c| ≤ Hvc/2 − 3Hg/4, inside which the model **with no box** is exactly linear: that is what
  makes `--harmonics (a)`'s −120 dB claim honest (measured −175 dB, rounding).
- **The stop.** |x| > Xmech after a substep clamps to ±Xmech and reverses the velocity with
  restitution 0.3 (assumed). At +30 dB and 25 Hz the 18W and 26W, whose fc sits above 25 Hz,
  press on the stop about half the time (38 353 and 62 506 hits in 2 s, the chatter of a
  pressed contact); the others bounce.
- **Box Qtc 0 is the open baffle.** Every preset's Qts is above 0.30 (the lowest is the 26W at
  0.309), so the bottom of the control is always "no box".
- **The drive.** Drive is dB over V_ref, the volts at which full-scale DC holds the cone at Xlin
  in a Qtc 0.707 box (7.7 V for the 10F up to 112.8 V for the B&C). Box Qtc does not move
  V_ref, so a stiffer box strokes less at the same Drive, as a real one does.
- **Measurement.** Every sine is a whole number of Hz over exactly 32 000 tap samples, so a
  single-bin DFT is exact and the bins orthogonal; steady states get a 1 s raised-cosine
  fade-in, 2.5 s to settle and a 1 s window. The closed forms (X/V with Le, the force
  compliance, k2 = γ(γ+1)P0 Sd³/2Vb², the box's rectification k2⟨x²⟩/Kt) are written out in
  the harness from the preset table, not taken from the plugin (`truthOf`).
- **The ripple's soft limit.** Ripple ×150 applied to a stop's ringing asked the surround's
  rim to move further than the roll can reach, and the roll's solve stretched it by orders of
  magnitude. The displayed rim motion now passes through 0.15 × roll × tanh(drawn / (0.15 ×
  roll)), so the roll's chord stays under 1.52 w < πw/2 and it can always bend. True-scale
  ripple (microns) is untouched; only the exaggeration saturates.
- **The table extrapolates past its end bins.** A 1024-bin table read between bin centres held
  the outermost half bin constant: at the driver's edge the clip sat up to half a bin off and
  `--rest` failed by 31 codes at 720p. `tableAt` extrapolates linearly from the last two
  centres; paint (`a.w`) is clamped to [0, 1] and the bare reflectance and gloss to ≥ 0.
- **Alpha: opaque.** Output alpha = mix(clip alpha, 1, Mix). The clip's alpha is the paint's
  coverage, so where the clip is clear the bare driver shows (cone 0.04, dust cap 0.06,
  surround 0.02, flange 0.30 reflectance; outside the driver the baffle, 0.02, under the clip).
  Checked on a ramp-alpha card (`--alpha`) and looked at on Resolume's BattleWeapon_Tank_09 and
  Trinity_09, which carry alpha: the tank is printed on a dark driver with a grey flange, never
  a hole.
- **The fallback** (no input, or the chosen one Opening, Missing or Failed): one sine per band
  at the band's geometric centre (63, 400, 1600, 6400 Hz), its amplitude the band's level
  clamped to [0, 1] and ramped linearly across the frame, continuous in phase; the number of
  samples from the host clock (dt × 32000 with a carry, at most 0.25 s a frame). Stated in the
  README, the spec and the label ("None (host FFT)"): it pumps, it cannot show the waveform,
  it is uncalibrated, and it is frame-rate dependent.
- **The clock** votes seconds or milliseconds against the host's step (ferrofluid's vote with
  fountain's patience), so a millisecond host clock at 4.99e8 drives the fallback's sample
  count correctly (`--fallback`).
- **The device menu (spasis's).** Values are `earshot::deviceKey(name)`, so a composition saves
  the name; 0 is "None (host FFT)"; "System default" is the key of `earshot::kDefaultDevice`;
  16 slots, the spares "(none)". A saved key met in no listing asks earshot for a name that
  cannot match, so it goes Missing (never the default); Rescan resolves it by name when the
  device returns. `selectDevice` only when the value changes (any select ends a `feed()`).
  `WOOFER_NO_DEVICES=1` keeps the list empty without asking CoreAudio (verify.sh's oxbow run).
- **A stalled host** drops the oldest audio: at most one second (`kMaxPull`) is taken from the
  tap in a frame.
- **Defaults** (below) moved from the spec's after Resolume's clips: Drive +6 → +18 dB,
  Ambient 0.35 → 0.2, Gloss 0.5 → 0.85.
- **Workflows only once public.** CLAUDE.md: Actions only on public repos. Until it was public
  woofer shipped none, so nothing could run by accident; `ci.yml` and `release.yml` came with
  the public repo (mirrorball's CI and filings' release, with mirrorball's `wincheck.ps1`).
- **The `massless` negative control of the spec is `lightCone` in code**, and it is not
  massless: Mms × 1e-2 (fc ten times higher). Mms × 1e-5 blew RK4 up (see the traps).
- **`--pumping` asks for 1000×, not the spec's 100×**: the real model's worst is 8 559.

## Defaults

Run over Resolume's bundled clips through `--pipe` with their own WAVs, looked at as stills
(scratch only; Resolume's media is not in the repo): Beat 001, Beat 005, Synth 003, Bass 002
(DXV, opaque), BattleWeapon_Tank_09 and Trinity_09 (alpha) and IntoTheGlow_21.

- **Resolume's demo WAVs are stems**, quiet: Beat 001 −21.9 dBFS RMS, Synth 003 −27.1, Bass 002
  −32.3; Beat, Bass and Synth 001 layered, −20.1. At the spec's +6 dB, Beat 001 moved the 30 cm
  1.17 mm (4 % of Xmech): the speaker read as a faint ring and nothing moved. At **+18 dB** the
  stem's kicks reach 4.67 mm (17 %), the layered stems 4.63 mm (17 %), and the same layers
  mastered to −11.6 dBFS RMS reach 11.2 mm (40 %) with no stop hit; at +24 dB the master hits
  the stop (8 hits in 7.5 s). Bass 002 barely moves a sub at any Drive (0.74 mm at +24 dB):
  its bass is above 100 Hz, where a 30 cm driver's stroke has fallen as 1/f², which is right.
- **On a dark clip the driver was barely there** (Ambient 0.35, Gloss 0.5). Ambient **0.2** and
  Gloss **0.85** give the roll a highlight arc, the dust cap a dome's glint and the cone a
  radial sheen, so a black clip (Synth 003, Bass 002) reads as a black loudspeaker and a busy
  one (Beat 005) still shows a cone. Light Angle 40° and Turn 135° were kept.
- **Observed, kept:** on an inward stroke the roll's uncovered foot shows a one-pixel specular
  rim all round (a near-vertical face seen at grazing, F → 1); Gloss scales it. DXV clips'
  "black" is video-range, about code 16.

## Cost

`wftest --bench4k`, the defaults on the harness's track, GPU time by `GL_TIME_ELAPSED`, median
of 120 frames after 120 of warm-up (4K: of 30, once), 16 November 2026, arm64 Release, the
machine otherwise quiet:

| raster | GPU | wall | integration | profile table | earshot |
| --- | --- | --- | --- | --- | --- |
| 720p | **0.38 ms** | 1.71 ms | 152 µs (533 samples, 0.28 µs each) | 319 µs (16 rows) | 596 µs |
| 1080p | **0.36 ms** | 1.55 ms | 129 µs (0.24 µs each) | 272 µs | 513 µs |
| 4K | **1.55 ms** | 2.97 ms | 129 µs | 272 µs | 517 µs |

The CPU side does not depend on the raster; 720p's GPU figure above 1080p's is the GPU's clock
ramping, not the work. The integration costs about **0.13–0.15 ms a frame** at 60 fps (two RK4
substeps and four exact mode updates per sample). earshot's `update()` is the largest CPU item,
about 0.5 ms, all of it analysis woofer does not use (earshot's own AGENTS.md: mean 0.6 ms);
there is no switch to turn it off.

## The traps actually hit

- **The ripple tore the surround.** See the soft limit above: the roll's length went from
  exact to wildly stretched only when a stop rang at Ripple ×150. `--surround` caught it.
- **The table's end bins** (above): 31 codes at the driver's edge in `--rest`.
- **A Python edit replaced the empty string.** Slicing to `s.index('void main()')` found the
  vertex shader's `main` first, the "old" text was empty, and `replace('', new)` interleaved
  the new text between every character: Shaders.cpp went to 3.5 MB. Recovered by deleting
  every copy of the new text (back to 5636 bytes) and reapplying from the fragment shader's
  start. Every scripted edit since asserts `count(old) == 1` first.
- **The flattop claim was in the wrong regime.** At fc/2 (the spec's frequency) the cone is
  damping-controlled: Bl falling past Xlin removes electrical damping, the ends run out faster
  and the crest **rises** above √2 (1.48–1.57, printed as a note). The claim is now made where
  it is true, stiffness-controlled at fc/4 in a Qtc 0.707 box.
- **The drift claim met the stop and an instability.** At 3fs and 1.6 Xlin the stroke hit the
  stop and, offset or not, left the symmetric orbit. Restated to fs/2 at 1.6 Xlin (in) and 2fs
  at 1.0 Xlin (out), every run checked off the stop. The instability is printed as a note: the
  30 cm, **offset 0**, at 2fs and 1.2 Xlin sits at a mean of −1.20 Xlin.
- **A reset model underflowed the harness.** `collectTrace` took `Count() − seen` unsigned; the
  resize check's negative control resets the model, the difference wrapped to ~2⁶⁴, and the
  harness pushed until it held 36 GB and died. stdout into a pipe is block-buffered, so the
  whole `--negative` run vanished with no output twice, the second time behind `| tail`,
  whose exit 0 looked like success. Fixed (a reset restarts the count; more than the ring
  since the last look aborts loudly), stdout is line-buffered, and `WFTEST_NEGATIVE=name`
  runs one check's wrong models. A full `--negative` takes 28 s; the broken one ran 8 min.
- **The sweep landed on a kick's first instant.** At 180 frames (3.00 s, on the beat) the cone
  was where the bass note alone put it: Coil Offset read dead and Xmax 0.006. At 183 frames
  (50 ms into the kick) they read 4.0 and 4.8, with no Drive context needed.
- **Raising the default Drive broke `--devices` on one renderer only.** At +18 dB the scripted
  Test Mic (50 Hz, half scale) drives the 30 cm onto the stop, and each hit flashes the lamp
  white: on the GPU the lamp was read between flashes, on the software renderer (51 ms frames)
  it read white. The check now sets Driver, Drive (0 dB) and the lamp itself and requires no
  stop hit, and its slow-open limit is 100 ms over the longest frame before the select, since
  the renderer's own frame cost (2.5 ms GPU, 49 ms software) is not the open's.
- **A blown-up model passed `--pumping` without GL.** The massless control (Mms × 1e-5) put
  fc near 10 kHz and made RK4 at 64 kHz unstable: both peaks read infinite, and
  `inf >= 100 * inf` is true. On the GPU the Height half of the check failed it anyway, so
  `--negative` was green there and only `WFTEST_NO_GL=1 --offline` showed it undetected. The
  check now requires both peaks finite, the control is Mms × 1e-2 (stable, fc ten times
  higher: ratio 91), and the threshold is 1000× (real model ≥ 8 559), so 91 fails by 11×.
- **One frame outgrew the trace, on the software renderer only.** The device path takes every
  tap sample queued, up to a second; the scripted backend delivers at 4× real time, so a
  50 ms frame brings 6 400 samples and the frame after Test Slow's open more than the ring's
  8 192. The harness's own guard (from the 36 GB trap) aborted `--devices`. It had passed
  in the run before, so it is timing. The devices rig now skips and counts the gap (448
  samples, printed as a note); nothing there reads x, and the model checks max |x| and the
  stop per substep. Every other rig still aborts on a gap.
- **verify.sh failed silently on it.** `printf "$out" | grep FAIL | sed` under `set -e` and
  `pipefail`: an abort prints no FAIL line, grep exits 1, and the script ended `exit 1` before
  its own `fail` message. The three such pipelines now fall back to the output's last lines.
- **Apple's software renderer drew the same frame differently under load.** `--resize`
  (software) failed once in verify at 2.4e-4 codes, with the trace and every uniform the same
  in both rigs, and passed 10 of 10 run alone. Run 14 at once on 16 cores: 2 of 28 failed, 3
  and 17 pixels 5.5e-7 and 6.1e-5 codes apart, near the bottom of the frame. On the GPU it
  stays 0 in every run. The software comparison now allows 0.001 codes; a real resize bug (a
  stale size, a reset driver) is whole codes, and `resetOnResize` is still caught by the
  trace. The check now prints how many pixels differ and where.
- **The status lamp is smaller than the sweep's floor**, by design (12 pixels at 320x180): it
  has its own floor, with the arithmetic beside it in `sweep.py`.
- **The SDK has no `GetParamElementValue`**: `elementValue()` reads the element's default.
- **`readWav` looped forever on a 0-bit header** (a zero-sized frame): now refused.
- **macOS make's one-second mtimes**: `mutate.sh` deletes every woofer object before each
  mutant rather than trusting a restored file to look newer.
- **zsh has no PIPESTATUS** (and does not word-split an unquoted variable): verify.sh and
  mutate.sh are bash.

### earshot, as a first consumer found it (nothing patched)

- **A Silent device still reports `Source::Device`.** The lamp must read `CaptureState` to show
  steady red; the source alone says green.
- **Any `selectDevice` ends a `feed()`.** Selecting "" at startup would silently stop the
  harness's audio, so woofer selects only when the parameter changes.
- **The scripted `CaptureBackend` delivers at 4× real time.** `--devices` waits on states, not
  on wall time, except the one 100 ms limit (a third of a 300 ms open, over the frame's own cost).
- **The Linear host-bin law gives the 30–150 Hz band no bin** (documented by earshot): woofer's
  first band starts at 20 Hz and ends at 200.
- **Host band levels are not bounded by 1** (they are root-sum-squares of bins): woofer clamps.
- **`update()` runs all the analysis** (thirds, onsets, pitch, tempo) whatever the consumer
  reads: ~0.5 ms a frame on the render thread for a plugin that wants only the tap.
- The tap passes DC exactly, which `--magnify` relies on.

## Verified, and assumed

Verified, by `tools/verify.sh` on 16 November 2026 (numbers in README's Status): every claim in
the table there, at 1280x720 and 320x180 on this Mac's GPU, the picture checks again on Apple's
software renderer; the physics checks with no GL context; the negative controls and mutants;
the sweep; the bundle universal, signed ad hoc, read and rendered by oxbow.

Assumed: the cone's straight 25° profile, the dust cap's dome, the half-roll surround of width
Xmech; Kms(x) stiffening 4× by Xmech; the stop's restitution 0.3; the fringe field Hg/4 each
side; the breakup modes' frequencies (read by eye off response plots), shapes, Q and
participation; B&C's Xmech (23 mm, not printed); the bare materials' reflectances; that
Resolume's FFT levels are anywhere near 0..1 for music.

Not done: a live device; music inside Resolume beyond Arena's demo stems on its FFT (which
moves the cone, a little at the default Drive: README "In Resolume"); presets, an OpenFX port. Windows is built by CI (MSVC,
no warnings in woofer or earshot) and gated in Arena on win-lab; the user guide is
docs/USER-GUIDE.md and the browser demo is demo/.

## Would this hold on another rasteriser, at another raster?

One line per check.

- `--smallsignal`, `--harmonics`, `--flattop`, `--drift`, `--surround`: no GL; IEEE double on
  the CPU; run under `WFTEST_NO_GL=1` by `--offline` to prove nothing in them reads GL.
- `--xmech`: the trace is CPU; the Height view's dust cap is a code compared with one code of
  slack, at Ripple 0 so the ripple's drawing cannot enter it.
- `--framerate`: the trace is bit-exact anywhere; the frame comparison is within one context.
- `--pumping`: the ratio is CPU; the Height view's 8-code and 1-code thresholds are far from
  the measured 90.5 and 0.000.
- `--rest`: 1 code, the 8-bit quantisation; Gloss 0, because GGX's `pow` and the sRGB curve may
  differ in the last bits elsewhere. Passes on the software renderer.
- `--magnify`: 0.5 px, a resampled step's half-level crossing; the table bin is under 1 px at
  both rasters. Passes on the software renderer.
- `--alpha`: 1 code. Passes on the software renderer.
- `--resize`: bit-exact on this Mac's GPU, same context only (honeydew's trap); elsewhere the
  claim is the same frame, not the same bits. Apple's software renderer is not bit-reproducible
  under load, so there the picture is held to 0.001 codes (the trace stays exact).
- `--fallback`: the frequency, the sample count and the clock are state; the lamp's colour is
  read off the picture to the code. Passes on the software renderer.
- `--devices`: state, and the lamp off the picture; one wall-time limit, generous by design.

## The negative controls and mutants

`wftest --negative`, each wrong model required to fail its check (19, all caught, 28 s):

| check | wrong model |
| --- | --- |
| smallsignal | the box's force dropped (`noBox`) |
| smallsignal | the coil's inductance dropped (`noLe`) |
| smallsignal | back-EMF with the wrong sign |
| harmonics | Bl a parabola, no plateau |
| harmonics | the air spring linearised |
| xmech | no stop at Xmech |
| framerate | one step a frame on the frame's mean voltage |
| pumping | a light cone: Mms × 1e-2, fc ten times higher (`lightCone`) |
| flattop | Bl flat and the suspension linear |
| drift | Bl symmetric about rest whatever the offset |
| drift | the air spring linearised |
| surround | the roll stretched (a semicircle on its chord) |
| rest | painted by material radius, ignoring the perspective |
| magnify | an orthographic camera |
| alpha | the clip's alpha passed through |
| resize | a new raster resets the driver |
| fallback | the fallback reads the (empty) tap |
| fallback | a millisecond host clock read as seconds |
| devices | menu values are positions, not keys |

`tools/mutate.sh`, 17 one-character mutants of the shipped code, each built arm64 and run at
320x180 under a 300 s alarm with `WFTEST_FIRST_FAILURE=1`, all caught (88 s in all): GLSL —
the clip fetched mirrored through the centre, the table read a bin off, the paint read from the
normal's z, the diffuse normalisation doubled (`--rest`), the Height view's rest at 0.6
(`--pumping`), the driver transparent (`--alpha`); C++ — the air spring pushing the wrong way,
Catmull–Rom's tangent, α = (Qtc/Qts)² − 2 (`--smallsignal`), the adiabatic exponent's sign
(`--harmonics`), the coil's offset applied backwards (`--drift`), the pinhole's depth sign
(`--magnify`), the roll's arc solve (`--surround`), the fallback's sines 1.5× too high, the
clock-unit vote upside down, the amber lamp off-colour (`--fallback`), a remembered key resolved
backwards (`--devices`).

## The sweep's honest list

`python3 tools/sweep.py --bare` (every per-control context dropped; source flags such as
`--left-only` kept, since they are what the control acts on): **nothing goes dead.** Breakup
and Ripple keep their contexts (Ripple 1 or Exposure 0) in the normal run only to be measured
more clearly. Audio Input and Rescan Inputs are not swept (no device may be touched); they are
`--devices`'. The lamp is held to its own floor (above).

## Open questions

- **Does Resolume save an option's value or its index, and does it clamp values to the element
  count?** Audio Input's values are name hashes (spasis's scheme). If Arena saves the index, or
  clamps, a composition will not find its device by name. Unverifiable without a host.
- **What do Resolume's FFT bins hold for music?** The fallback assumes levels near 0..1; at the
  default Drive a level of 1 puts the 30 cm on the stop. FFT Law is exposed because nobody has
  measured the bins. On Arena's Beat 001 (−22 dBFS RMS) the default Drive and law gave peaks of
  only 5.6% of Xmech, and Log Power 69% (2026-11-19): a stem sits well under 1 there. Whether the
  default law or Drive should change for the fallback is open.
- **Is +18 dB the right default Drive?** It suits stems and a −12 dBFS loopback; a −8 dBFS
  master will hit the stop on most kicks (physical, and loud).
- **Should earshot offer a tap-only mode?** About 0.5 ms a frame of analysis woofer ignores.
- **The B&C's Xmech (assumed 23 mm) and the 18W's Vas (recomputes 11 % above print)**: both
  are datasheet gaps, written down in the spec.
- **CI.** None here (above). fountain's unrun `ci.yml` with its "delete if private" header is
  still in fountain; whether either repo is ever public is Allan's call.
- **Registration.** The About headers and ATTRIBUTIONS.md are hand copies until woofer is in
  the website's projects.json and stoatworks-backend.

## Browser demo

https://woofer-demo.stoatworks-labs.com/ (2026-11-18), from `demo/`, deployed by
`wrangler.toml` (a Worker route: the zone is at Cloudflare's 100 custom domains, so
`woofer-demo` is a proxied AAAA `100::` record made through the API) and
`.github/workflows/deploy.yml` on every push to main. mirrorball's and fountain's pattern.

- **It runs the plugin, it doesn't port it.** `demo/woofer-core.wasm` is `Woofer.cpp` and
  everything it calls (Driver, Profile, Audio, Controls, Shaders, Diag), earshot's snapshot
  (every `src/*.cpp` but `Miniaudio.cpp`/`MiniaudioImpl.cpp`, with `NoCapture.cpp`: its own
  `EARSHOT_CAPTURE=OFF` build, so the module has no device backend at all) and the FFGL SDK's
  plugin classes, FFGLLog, FFGLShader and FFGLUtilities, compiled unmodified by emscripten
  (`demo/tools/build-wasm.sh`). The integrator, the profile table, the fallback and earshot's
  resampler and tap are that C++. Left out: `PluginEntry.cpp`. The script fails if a
  `source/*.cpp` or an earshot `src/*.cpp` it doesn't list appears.
- **What is the page's**: `demo/wasm/glue.cpp` plays the host (constructs the plugin, reads
  its declarations back through the SDK so the panel is built from them, forwards
  SetFloatParameter, SetTime, the Audio buffer's elements and ProcessOpenGL as wftest's rig
  does, and hands the page the harness's accessors: the driver's model, x, max |x|, Xlin,
  Xmech, stop hits, samples a frame, earshot's state, the plugin's clock);
  `demo/wasm/gl_shim.cpp` takes glShaderSource (refusing any text that isn't byte for byte the
  page's assembly of `demo/shaders.js`, then the kit's `port()`) and
  glEnable/glDisable/glIsEnabled (GL_PROGRAM_POINT_SIZE, which WebGL2 lacks); the host row; the
  read-outs' units, tooltips, the eleven presets (in dB, factors, seconds and degrees, through
  the plugin's own inverses via `wf_param_from`), the line under the picture and Diag's log
  from emscripten's memory file system (`WOOFER_LOG_DIR=/woofer/logs`). Off the panel: Audio
  Input, Rescan Inputs, the Audio buffer and the About block (the panel's note says so).
- **The audio, never a device.** The page never calls a device API (`check_shaders.py` fails if
  a served file names getUserMedia, enumerateDevices or mediaDevices), the module cannot open
  one, and `wf_prepare` sets `WOOFER_NO_DEVICES=1` so `AudioInput::Refresh` never asks. The host
  row's choices are wftest's: the track (`musicAt` and its five helpers copied from
  `tools/wftest/main.cpp` into the glue, held character for character by `check_shaders.py`,
  with kFeedRate, the types and the `--host-fft` loop, formula and setter lines), `--left-only`,
  `--silent` (earshot goes Silent after 3 s, the lamp steady red), and `--host-fft` (nothing
  fed; the fallback on the synthetic spectrum, the lamp amber); plus a file of the visitor's,
  decoded by `decodeAudioData` in an `OfflineAudioContext` at 48 kHz (no output device), mono
  to both channels, fed through `wf_feed` and looped (wftest --wav plays once). Each frame
  feeds the samples up to the page's clock × 48 000. Nothing is played aloud. Every audio
  change is a new instance from time 0 (a feed lasts until a selectDevice, which nothing
  here calls), and the page's default is the track where the plugin's own is "None (host
  FFT)".
- **The clock.** The page sends seconds from frame deltas (capped at 0.1 s). Each new instance
  (load, a change of audio, Restart) is primed with five frames about a millisecond apart so
  the plugin's seconds-or-milliseconds vote settles before the first frame shown (mirrorball's
  fix): on the feed path nothing is fed during them, so a paused page stands still. On the
  host-FFT choice the fallback counts samples off the plugin's own clock, which is the wall
  clock until the vote settles, so the prime gives a new instance about 6 ms (some 190 samples)
  of the spectrum, as a host's first frames would; wftest presets its clock and never does.
- **Photosensitivity.** The page's own audio through `wftest --pipe` at 320×180, 60 fps, over a
  flat grey and over colour bars, measured with xenon's flashcheck rule (whole frame and 5×5
  third-of-frame windows; over six transitions in a second is over the line): the **Lit**
  view made 0 transitions at the default Drive and at +30 dB, on the track, the left-only
  track and the host-FFT spectrum; the **Height** view made 10 (whole frame) and 17 (worst
  window) on the track at the default Drive, 16 and 21 at +30 dB, 0 and 10 on the left-only
  track, 6 and 12 on the host-FFT spectrum (8 and 15 at +30 dB). The release survey's figures
  (33 of Resolume's clips: Height over the line on 10 with a master at about −9 dBFS RMS,
  worst 22; Lit under on all 33 at default and full Drive) are on the page with these. So the
  page opens in Lit (the plugin's default, unchanged), no preset uses Height (checked at load;
  the page throws if one does), a link opening in Height opens paused, and a caution line shows
  whenever View is Height. Embed mode (`?embed=1`) shows neither and applies no rule.
- **Checked by `tools/verify.sh`** (`demo/tools/check_shaders.py`): the four GLSL pieces
  character for character and the order AssembleVertex()/AssembleFragment() join them (read
  from Shaders.cpp and InitGL's `Compile( vertex, fragment )`); all 21 uniforms set by the
  setter their type takes and nothing undeclared set; wftest's track in the glue; no served
  file naming a device API; the hashes of every input the .wasm was built from
  (`demo/wasm/inputs.sha256`, 55 files) and the SDK pin. Any change under `source/` or
  `external/earshot/` needs `demo/tools/build-wasm.sh` rerun and its outputs committed (mode
  644).
- **Compared with wftest, once (2026-11-18), by `demo/tools/crosscheck.py`** (in the repo;
  nothing runs it by itself: it needs Chrome, cdpshot and a built wftest). Headless Chrome on
  the real GPU (ANGLE on Metal), the page paused, the settings applied, Restart, then N presses
  of Step (800 N samples fed, the clock at N/60); Mix 0 hands the clip back so the canvas is the
  exact input frame; `wftest --pipe --fps 60` gets that frame N times with the same host values,
  and its last frame (fed the same 800 N samples) is compared. Apple M4 Max, 640×360 (691,200
  colour values a case):

  | case | clip | N | colour values off | max | stop hits, page / wftest |
  | --- | --- | --- | --- | --- | --- |
  | defaults, the track | scene | 330 | 423 | 1 code | 17 / 17 |
  | 10 cm full-range at +6 dB | bars | 180 | 17 | 1 code | 0 / 0 |
  | Height view | grid | 240 | 48 | 1 code | 12 / 12 |
  | open baffle, Ripple 1000, left only, Channel Left | detail | 200 | 973 | 1 code | 52 / 52 |
  | 18 in PA sub at +30 dB, Exposure 0, Mix 50% | spot | 150 | 565 | 1 code | 313 / 313 |
  | None (host FFT), defaults (reported, not judged) | scene | 120 | 159,775 | 117 codes | 12 / 17 |

  Alpha identical in every case, and the tap-sample counts identical in the five feed cases
  (175,988 on the defaults). The negative control (wftest with Drive one slider step, 0.001,
  off on the defaults case) puts 2,526 values off, up to 10 codes. So on the feed path the
  wasm integrator lands on the Mac's to within the GPU's last code even through hundreds of hits
  of the stop. The host-FFT case differs for the clock reason above: the page's instance had
  64,195 tap samples against wftest's 63,999 (the run-to-run spread is the prime's wall time).
- **Also checked on the page** (CDP, the real GPU): the defaults read back as the plugin's
  (30 cm Subwoofer, 0.707, ×1.00, +0.10 gap, ×1.00, L+R, +18.0 dB, Linear Mag, ×0.95, ×1.50,
  8.0 ms, 150×, 40°, 135°, 0.20, 0.85, On, Lit, 100%); the coil moves with the track (−13 to +21 mm over
  a second), the lamp is green on the feed, steady red after 3 s of zeros, amber on the host
  FFT; Driver changes the picture paused, Drive changes it playing (the negative control
  above); a WAV chosen through the file input decodes and drives the cone; all eleven presets
  render with no GL error and Defaults gives the first frame back exactly; `?view=1` opens
  paused with the caution; no horizontal scroll at 375 px; no console errors.
- **Every gap, as the page discloses it** (banner, the panel's note, the "does not reproduce"
  list): it runs the plugin, not a port, and the units, tooltips and presets are the page's;
  no audio device or microphone, earshot built without capture, Audio Input, Rescan Inputs and
  the Audio buffer off the panel, the live-device path absent; the audio is the harness's track
  or a file decoded, resampled to 48 kHz and looped in the page, never played aloud; "None
  (host FFT)" runs on a synthetic spectrum, not a real FFT, and is uncalibrated; every audio
  change is a new instance from time 0, and the page's default audio is not the plugin's; GL is
  emscripten's WebGL2 with four shimmed entry points and the kit's GLSL port; the clock is the
  page's and primed, and a paused control move feeds nothing; a frame is a display refresh
  (the feed path does not depend on it, the fallback does); WebAssembly has no fused
  multiply-add and musl's libm; the clip is the kit's and premultiplied, so the kit's alpha
  clip is left out; the warning, caution and open-paused rule are the page's; the About block
  is off the panel; nothing is measured, and the plugin has never been in Resolume or opened a
  device.
- **Traps hit**: `ffglex/FFGLShader.cpp` calls `ffglex::Log`, which is in FFGLUtilities.cpp
  (an undefined symbol at link until it was added); the kit's canvas has no
  `preserveDrawingBuffer`, so a paused page reads back zeros unless the attribute is forced
  from outside (`Page.addScriptToEvaluateOnNewDocument` with a `getContext` override); the
  kit's inspector note says every declared parameter is on the panel, so the page appends
  which four are not; the host-FFT spectrum's lines live in `tools/wftest/run.inc`, not
  main.cpp, so the harness check reads all three of wftest's files; the read-out column fits
  about ten characters, so units went into the tooltips.
