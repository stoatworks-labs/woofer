# woofer

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. **It has never been loaded into
> Resolume, and it has never opened a live audio device.** What's established is established
> by an offline harness, `wftest`, that drives the real plugin class through the same
> `ProcessOpenGL` a host calls, in a headless GL context on a synthetic clock, on this Mac's GPU
> at two rasters, and feeds it audio through the same earshot `feed()` path a device would use.
> The cone's stroke matches the sealed box's closed-form transfer function, coil inductance
> included, to **3.2e-4** over 24 sines from 10 Hz to 2 kHz on all five drivers; the coil
> **never passes Xmech** at any substep; a sine inside the motor's linear range makes no
> harmonic above **−175 dB**, and in a box the air spring's second harmonic is the one
> second-order perturbation theory predicts to **1e-5**. The same audio at 24, 30, 60 and 144
> fps gives a **bit-identical** trace. **19** deliberately wrong models and **17** one-character
> mutants of the shipped code are all caught. What the cone looks like beyond the datasheets'
> numbers (its profile, its breakup modes, how hard the stop is) is **chosen**, not measured:
> see [Status](#status).

A loudspeaker for [Resolume](https://resolume.com) Arena and Avenue, as an FFGL effect,
**SW Woofer**: your clip is printed on the front of a real woofer, and the music drives the
woofer.

![A 30 cm woofer seen face-on, filling most of the frame's height, with a city skyline at
dusk printed across its cone, dust cap and surround and carrying on unprinted-on across the
baffle around it. The cone is mid-stroke toward the camera: the skyline on it is magnified
and smeared radially by an 8 ms exposure, the surround's roll is creased where it bends, and
a glossy highlight sits on the dust cap and the upper left of the roll](docs/hero.png)

<sub>One frame rendered by `wftest`, not captured from Resolume: the defaults on the harness's
card, 5.5 s into its track of kicks, hats and a bass note (`--frames 330`).</sub>

## The one idea

**The clip is printed on the front of a loudspeaker, and the music drives the loudspeaker.**

Nothing is animated by a parameter. A cone driver set by the Thiele–Small parameters of a real
datasheet sits in a sealed box and is voltage-driven by the audio waveform. Its coil and cone
are integrated at the audio rate (32 kHz, RK4, two substeps a sample), with the motor's force
factor falling as the coil leaves the gap, the suspension stiffening, an adiabatic air spring,
a hard stop at the maximum mechanical excursion, and four breakup modes. The stroke moves the
printed clip toward and away from a pinhole camera, the surround's roll bends to follow it
without stretching, and the whole front is lit as a heightfield.

## What falls out

- **Kicks pump the cone; hi-hats leave it still.** Above the box's resonance the stroke falls
  as 1/f², so a kick moves the cone millimetres and a hat at the same level moves it microns
  (8 559 to 89 030 times less, measured). Nothing in the code looks at frequency.
- **A big hit flat-tops.** Past Xmax the coil leaves the gap and the force per amp falls: the
  stroke compresses and its peaks flatten.
- **The coil bottoms.** At ±Xmech the stroke stops, whatever the drive, and the status lamp
  flashes white.
- **An overdriven cone drifts.** With the coil set off the gap's centre, the stroke's mean walks
  toward the strong side below resonance and away from it above. A small box pushes it outward
  on its own.
- **The clip stretches and creases.** At rest the picture is exactly the clip. A cone coming
  toward the camera magnifies its part of the clip; the roll's printing compresses over its
  crest; on a full inward stroke the foot the printing never reached shows bare.
- **The cone rings.** Breakup modes ripple its surface, sharply on the aluminium sub near 4 kHz,
  broadly on the paper cones. **Ripple** exaggerates them as a laser vibrometer's plot does;
  at true scale they are microns.

## Where the audio comes from

From an audio input, through [earshot](external/earshot/README.md), the fleet's live-audio
front end (a snapshot of commit `8124191`, unmodified). Pick the input in **Audio Input**: it
is remembered by name, so a composition reopened on another day finds the same device, and a
device that has gone shows as missing rather than quietly becoming another one. **Rescan
Inputs** lists what is plugged in now. The device is opened off the render thread.

**On macOS the microphone permission belongs to Resolume, not to this plugin.** The first time
any plugin inside Resolume opens an input, macOS asks whether *Resolume* may use the
microphone. If that was refused, macOS hands Resolume silence rather than an error: the lamp
goes **steady red** after 3 s of exact zeros, and the fix is System Settings → Privacy &
Security → Microphone → Resolume Arena. An unpatched Dante receiver or an empty loopback looks
the same.

**With no input chosen ("None (host FFT)"), woofer falls back to Resolume's own FFT, and that
is much less.** The FFT gives four band levels a frame, no phase and no waveform, so woofer
synthesises a sine per band (63, 400, 1600 and 6400 Hz) whose level follows the band. On the
fallback the cone still pumps on the bass and stays still on the hats, and Bl(x), the stop
and the drift all still act; but **it cannot show the music's waveform** (two kicks of
different pitch look the same), **its level is not calibrated** (nobody has measured what
Resolume's bins hold; **FFT Law** chooses how to read them), and **it steps once a frame**, so
unlike the audio-input path it depends on the frame rate. The lamp is **amber** whenever the
fallback is driving.

The status lamp, a small LED below and right of the driver: **green** an input is delivering;
**amber** Resolume's FFT; **blinking red** the chosen input is missing or failed (the FFT
drives meanwhile); **steady red** the input is open but silent; a **white flash** on each hit
of the stop. **Status Lamp** turns it off.

## Controls

| Group | Control | What it does | Default |
| --- | --- | --- | --- |
| Driver | **Driver** | 10 cm full-range, 18 cm midwoofer, 26 cm woofer, 30 cm subwoofer (Scan-Speak), 18 in PA sub (B&C) | 30 cm Subwoofer |
| | **Box Qtc** | the sealed box, as its Q (0.30–1.50); at or below the driver's own Qts it is an open baffle | 0.707 |
| | **Xmax** | ×0.25–×2 the coil's overhang: how far it goes before leaving the gap | ×1 |
| | **Coil Offset** | the coil's rest position off the gap's centre, ±0.3 of the gap's height | +0.1 |
| | **Breakup** | ×0.25–×4 every breakup mode's frequency | ×1 |
| Audio | **Audio Input** | None (host FFT), System default, or a device by name | None (host FFT) |
| | **Rescan Inputs** | list the inputs again | |
| | **Channel** | L+R, Left or Right | L+R |
| | **Drive** | the amplifier: −20 to +30 dB over the voltage at which full-scale DC holds the cone at Xlin | +18 dB |
| | **FFT Law** | how the fallback reads Resolume's bins: Linear/Log × Magnitude/Power | Linear Mag |
| Look | **Size** | the driver's overall diameter, 0.3–1.5 × the frame height | 0.95 |
| | **Camera** | its distance, 1–5 × the driver's diameter: perspective | 1.5 |
| | **Exposure** | 0–40 ms of motion blur, the same at any frame rate | 8 ms |
| | **Ripple** | 0–1000 × display gain on the breakup modes | 150 × |
| | **Light Angle**, **Light Turn** | the key light's elevation (10–90°) and azimuth | 40°, 135° |
| | **Ambient** | the fill's share | 0.2 |
| | **Gloss** | specular on the moving parts | 0.85 |
| | **Status Lamp** | Off, On | On |
| | **View** | Lit, or Height (the axial displacement as grey) | Lit |
| | **Mix** | with the clip | 1 |

The output is **opaque**: alpha is mix(clip alpha, 1, Mix). Where the clip is transparent the
bare driver shows (a dark cone, a grey flange); outside the driver the clip carries on across
the baffle, unlit.

## Status

**v0.1.0, local only, 16 November 2026. Never loaded into Resolume; no live audio device ever
opened.**

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build (16 November 2026). Every check below runs at **two rasters**, 1280×720 and
320×180, and the picture checks again on **Apple's software renderer** at 320×180. Audio is
synthetic, fed through earshot's `feed()`; the device menu runs on earshot's scripted capture
backend. **No step opens or lists a real audio device.**

| check | result |
| --- | --- |
| `--smallsignal` | complex X/V by single-bin DFT over the steady state, against Bl/[(Re + sLe)(Mms s² + Rms s + Kt) + Bl² s]: 24 third-octave sines 10 Hz–2 kHz × 5 drivers × (Qtc 0.707 box, open baffle), worst **3.15e-4** relative at 2032 Hz (tolerance 1e-3, twice the Catmull–Rom midpoint error there) |
| `--harmonics` | (a) open baffle, half the Bl plateau: loudest of H2–H5 **−175.1 dB** re H1 (≤ −120); (b) Qtc 0.707 box: H2/H1 against k2 = γ(γ+1)P0 Sd³/2Vb² through the linear compliance at 2ω, ratio **0.99999–1.00001** (within 2 %) |
| `--xmech` | Drive +30 dB, 25 Hz sine and kicks, every driver: max \|x\| over every substep **= Xmech exactly**, the stop engaged (100–62 506 hits); in the Height view the dust cap never codes past Xmech |
| `--framerate` | 24, 30 and 144 fps against 60: **0 of 63 988** tap samples differ in x, v, i or any mode; the Lit frame at audio time 1 s is **0 codes** apart at 24 and 60 fps |
| `--pumping` | a kick and a 7 kHz hat at the same peak level: kick/hat stroke **8 559–89 030** (≥ 1000) on every driver; in the Height view at the defaults the kick moves the dust cap **90.5 codes**, the hat **0.000** |
| `--flattop` | Qtc 0.707 box at fc/4, linear prediction 1.8 Xlin, off the stop: the peak falls **19–29 %** short (> 6.25 %, Bl's deficit at Xlin), crest **1.327–1.367** (< √2 − 0.01); at a tenth of the plateau the crest is √2 within **4.8e-4** |
| `--drift` | (a) offset 0: mean **≤ 3.6e-16** Xlin; (b) offset +0.1 Hg: **−0.035 to −0.048** Xlin at fs/2 (in), **+0.076 to +0.096** at 2fs (out); (c) −0.1 Hg mirrors within **3.5e-7**; (d) Qtc 1.2: the air spring's rectified k2⟨x²⟩/Kt to **2e-5** |
| `--surround` | a full-stroke run: the roll's length within **6.5e-7** of πw/2 (≤ 3e-6, the polyline's chord deficit); unpainted area **0** at rest and at +Xmech, **3.1–6.2 %** at −Xmech |
| `--rest` | at silence, Ambient 1, Gloss 0: **0.000 codes** from the clip; at the defaults, outside the driver, **0.000** |
| `--magnify` | DC holds the 30 cm at Bl V/(Re Kms) to **1.4e-13**; a ring printed at 158.34 px appears at **162.92 px**, the pinhole says 163.01 (40.67 against 40.75 at 320×180; within 0.5 px) |
| `--alpha` | output alpha against mix(clip alpha, 1, Mix) at Mix 0, 0.5, 1: **0.000 codes**; where the clip is clear the bare driver shows whatever the clip's colour |
| `--resize` | 1280×720 → 960×540 after 60 frames: the trace unbroken, the next frame **0 codes** from a run at 960×540 throughout (on Apple's software renderer, within 0.001 codes: it is not bit-reproducible under load) |
| `--fallback` | no input, bin 0 pulsing: the cone pumps, its strongest frequency **63 Hz**, **64 000** samples over 120 frames at 1/60 s, the lamp **amber**; the same with a millisecond host clock at 4.99e8; the top band alone moves it **1.8e-5** as much |
| `--devices` | scripted backend: the menu by name and key; Running and green, no stop hit; frames during a 300 ms open take **2.2–3.2 ms** more than the longest frame before it (≤ 100); Silent and steady red; a saved device that has gone is Missing and blinks, nothing opened in its place; Rescan reopens it by name |
| `--pipe` | 2.5 frames in, exactly 2 out, with the track and with `--wav`; bad cues, settings and WAVs refused; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **19** deliberately wrong models, **19** caught |
| mutation | **17** one-character mutants of the shipped GLSL and C++, **17** caught |
| `tools/sweep.py` | all **19** sweepable controls measurably change the picture, with and without their contexts |
| shaders | both compile through `glslc`, not merely through Apple's driver |
| the bundle | universal (`x86_64 arm64`), exports `plugMain`, ad-hoc signs; `oxbow` reads `SW Woofer` / `WF01` / `effect` and renders through it |

Render cost: see [AGENTS.md](AGENTS.md#cost). macOS only.

### Not established

- **No host has loaded it.** Not Arena, not Avenue, on any platform; no sound from Resolume has
  reached it. `oxbow` (the fleet's test host) reads the bundle and renders through it, with
  devices disabled, which is all.
- **No live audio device** has been opened, by woofer or by earshot. The device menu, the
  states and the lamp are proved on a scripted backend.
- **Windows** has never been built, and MSVC has never compiled earshot.
- **The fallback is uncalibrated.** At the default Drive a band level of 1 in Resolume's FFT
  drives the cone onto the stop; what level real music gives there is unknown.
- **Chosen, not measured:** the cone's profile and the dust cap's and surround's shapes, the
  suspension's stiffening (no datasheet gives Kms(x)), the stop's restitution (0.3), the fringe
  field's width, the breakup modes (read by eye off the response plots; their shapes and
  participation are assumed), the B&C's Xmech (not printed), the bare materials' colours.
- **Left out:** vented boxes, Le(x), thermal compression, rocking and azimuthal modes,
  self-shadowing, sound out.
- The defaults were chosen over seven of Resolume's bundled clips through `--pipe`, with their
  own soundtracks and a mix of them mastered to −12 dBFS, looked at as stills, not in a show.
- No user guide, no presets, no OpenFX port and no browser demo.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Woofer.bundle` into `~/Documents/Resolume Arena/Extra Effects`. Restart
Arena and it is under Effects as **SW Woofer**.

Windows needs Visual Studio 2022 and vcpkg (for GLEW). It has never been built.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, the submodule pin and the earshot snapshot, a
fresh universal build with no warnings, the bundle's architecture, plist and ad-hoc signature, a
host's view of it through `oxbow` (required), every check at two rasters and the picture checks
on the software renderer, `--offline` with no GL context, the pipe, the negative controls, the
mutants, the sweep for dead controls and the render cost. `CLAUDE.md` lists the checks one by
one; `./build/wftest --all` runs them all.

A clip through it, with its soundtrack:

```bash
ffmpeg -i clip.mov -f rawvideo -pix_fmt rgba - \
  | ./build/wftest --pipe --fps 30 --size 1280x720 --wav clip.wav \
  | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - out.mp4
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
