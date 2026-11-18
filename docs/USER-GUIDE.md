# Woofer user guide

Woofer is an FFGL effect for [Resolume](https://resolume.com) Arena and Avenue that prints your
clip on the front of a real loudspeaker and lets the music drive the loudspeaker.

Nothing in it is animated by a parameter. A cone driver, set by the Thiele–Small parameters of a
real datasheet, sits in a sealed box and is driven by the audio waveform itself, as an amplifier
would drive it. Its coil and cone are worked out 32,000 times a second. **So kicks pump the cone
and hi-hats leave it still**, because above the box's resonance a driver's stroke falls as the
square of the frequency; nothing in the code looks at frequency. A big hit flat-tops as the coil
leaves the magnet's gap, the coil bottoms at its mechanical limit, and with the coil set a little
off centre an overdriven cone walks off centre. The stroke carries your clip toward and away from
the camera, so it swells and shrinks, the rubber surround's roll creases the print as it bends,
and the whole front is lit as a glossy relief. At rest the picture is your clip.

![A 30 cm woofer seen face-on, filling most of the frame's height, with a city skyline at dusk printed across its cone, dust cap and surround and carrying on across the baffle around it. The cone is mid-stroke toward the camera: the skyline on it is magnified and smeared outward by the exposure, the surround's roll is creased where it bends, and a glossy highlight sits on the dust cap](hero.png)

*One frame from the offline harness, not captured from Resolume: the defaults over the harness's
card, 5.5 seconds into its track of kicks, hats and a bass note.*

> **Before you rely on this:** it's at **v0.1.0**, the first release, and early days. Everything
> below was compiled, rendered and measured offline on an Apple Silicon Mac (macOS 26), by a
> harness that drives the plugin's own class in a headless GL context, at two rasters (1280×720
> and 320×180) and partly on Apple's software renderer, feeding it audio through the same path a
> device would use. The cone's stroke matches the sealed box's textbook transfer function to
> 3.2 × 10⁻⁴ over 24 sines from 10 Hz to 2 kHz on all five drivers; the coil never passes its
> mechanical limit; and the same audio at 24, 30, 60 and 144 fps gives the same stroke, bit for
> bit. Nineteen models that are wrong on purpose and seventeen one-character changes to the
> shipped code are all caught, and every control that has pixels changes the picture.
> In Resolume it has run only on one layer over a still: in Arena 7.29.0 on a Mac, and in Arena
> 7.27.1 on Windows on software rendering. There it registers, shows its controls with the
> declared defaults and renders, and every control that acts on a still cone changes the
> picture. **In Arena on the Mac, Resolume's own spectrum didn't move the cone at all** (see
> [Where the audio comes from](#where-the-audio-comes-from)), and **no live audio input has ever
> been opened**, so the cone has never yet moved to music inside Resolume. It has **never been
> in a show**. The cone's shape, its breakup and how hard the stop is are chosen, not measured
> (see [Known limits](#known-limits)). Try it on a spare layer, with your input, before you put
> it in a show.
>
> This codebase was created with AI assistance, directed and reviewed by a human author.

> **Photosensitivity.** The default Lit view didn't add a single flash to any of Resolume's 33
> demo clips, at the default Drive or at full Drive. **The Height view can flash**: it shows the
> cone's stroke as grey across the whole driver, and with a loud master (about −11 dBFS RMS or
> hotter) it crossed three flashes a second over large areas on 5 to 10 of the 33. See
> [Flashing](#flashing) for the rates, and check your settings before you put this in front of an
> audience.

---

## Installing

Every download carries one effect, **SW Woofer** (`Woofer.bundle` on macOS, `Woofer.dll` on
Windows). Drop it into Resolume's effects folder and restart Resolume:

```
macOS    ~/Documents/Resolume Arena/Extra Effects/
Windows  %USERPROFILE%\Documents\Resolume Arena\Extra Effects\
```

Avenue uses the same layout under its own folder name. The effect then shows up in the effects
browser as **SW Woofer**.

The macOS download is a universal build (Apple silicon and Intel), as a `.dmg` or a `.zip`. It's
**Developer ID-signed and notarised**, so the bundle just loads. The Windows download is an x64
installer or a `.zip`. The installer offers Arena's folder above, or Avenue's on a machine that
only has Avenue; change it on the folder page if you use the other one. It isn't code-signed, so
it trips SmartScreen once: **More info** → **Run anyway**. Or skip the installer, take
`Woofer.dll` out of the `.zip` and drop it into the folder by hand.

---

## Start here

1. Put **SW Woofer** on a clip. You see a 30 cm subwoofer face-on, your clip printed on its
   cone and carrying on across the baffle around it, with a small amber lamp below and to the
   right.
2. **Give it music.** With **Audio Input** at **None (host FFT)**, the default, woofer listens
   to Resolume's own spectrum of the composition's audio (the **Audio** control picks which
   audio, as on any Resolume effect). Play a clip or a deck with sound and the cone should pump
   on the bass, but in the one Arena it has been tried in, on a Mac, it didn't move at all: see
   [Where the audio comes from](#where-the-audio-comes-from). For the real thing, pick an input
   by name in **Audio Input**: woofer then drives the cone with that input's waveform, and the
   lamp turns green.
3. **Drive** is the amplifier. At the default, +18 dB, a kick in Resolume's quiet demo stems
   moves the sub about a sixth of the way to its stop, and a loud master about 40% of the way.
   Turn it up until the lamp flashes white on the loudest hits: that's the coil hitting its stop.
4. Try **Driver** at **10 cm Full-range**: a small, light cone with a higher resonance, set from
   its own datasheet like the others.
5. **View** at **Height** shows only the stroke, as grey: mid-grey at rest, lighter toward the
   camera, darker away. It's the plainest way to see what the cone is doing (and it can flash:
   see [Flashing](#flashing)).

The defaults: the 30 cm subwoofer (Scan-Speak 30W/4558T00) in a sealed box of Q 0.707, the coil a
tenth of the gap's height out of centre, Drive +18 dB; the driver 0.95 of the frame's height
across, seen from 1.5 of its diameter away, with an 8 ms exposure; the key light 40° up and
turned 135° round the frame, Ambient 0.2, Gloss 0.85; the status lamp on; Mix 1.

---

## Recipes

**The kick, and nothing else.** The defaults, with music that has a kick. The cone pumps on the
kick and stays still on the hats, cymbals and voice. That's the driver, not a filter.

**Bottom it out.** Drive up to **+24 dB** or more on a loud track. The peaks flatten (the coil is
leaving the gap, so the force per amp falls), then the coil hits its stop and the lamp flashes
white on each hit.

**A drifting cone.** **Coil Offset** at **+0.3** and Drive high: on a sustained bass note the
cone's middle walks inward below the box's resonance and outward above it. A small box
(**Box Qtc** 1.2 and up) pushes it outward on its own, as the air in a sealed box does.

**An open baffle.** **Box Qtc** all the way down: at or below the driver's own Q it's a driver
on a bare board, with no air spring behind it. The bass moves it further and it rings less.

**Show the breakup.** **Ripple** up toward 1000 and a busy track with treble in it: the cone's
breakup modes, a few microns in real life, ripple across the paper. **Breakup** moves every mode
up or down in frequency.

**Up close.** **Camera** down toward 1 and **Size** up: the pinhole camera's perspective
exaggerates the stroke, so the dust cap swells toward you on the kick.

**A crisp cone.** **Exposure** 0 freezes each frame. The default 8 ms smears the print outward on
a fast stroke, as a camera's shutter would.

**Woofer on a band's PA.** **Driver** **18 in PA Sub** (B&C 18SW115-16): the biggest cone, in
the same box Q. Drive is reckoned against each driver's own linear travel, so you don't have to
turn it up to move a bigger driver.

---

## Flashing

People with photosensitive epilepsy can have a seizure from flashing light, and the line that
broadcast guidance (Ofcom) and web guidance (WCAG 2.x) draw is **no more than three flashes in
any second** over a large area of the picture. A flash here is a rise and a fall in brightness of
at least a tenth of white; two make a flash, so the line is six rises and falls.

Measured from the plugin's own output over Resolume's 33 demo clips at 60 fps, over the whole
picture and over every third-of-the-picture patch, with the clips' own audio (Resolume's demo
stems, quiet: −18 to −42 dBFS RMS) and with the same stems layered and mastered loud:

| View | Drive | audio | clips over the line | worst second |
| --- | --- | --- | --- | --- |
| Lit (default) | +18 dB (default) | own stems | 0 of 33 | 6, the clip's own (bare: 6) |
| Lit | +30 dB (full) | own stems | 0 of 33 | 6, the clip's own |
| Lit | +18 dB | mastered, about −11 dBFS RMS | 0 of 33 | 6, the clip's own |
| Lit | +18 dB and +30 dB | limited hot, about −9 dBFS RMS | 0 of 33 | 6, the clip's own |
| Lit | +30 dB, Exposure 0, or the driver filling the frame | about −9 dBFS RMS | 0 of 33 | 6, the clip's own |
| **Height** | +18 dB | own stems | 0 of 33 | 0 |
| **Height** | +18 dB | about −11 dBFS RMS | **5 of 33** | **19** |
| **Height** | +18 dB | about −9 dBFS RMS | **10 of 33** | **22** |
| Lit | +18 dB | the fallback, on the harness's synthetic kick spectrum | 0 of 33 | 6, the clip's own |
| **Height** | +18 dB | the fallback, on the harness's synthetic kick spectrum | **33 of 33** | **9** |

The last two rows are the fallback (**None (host FFT)**) fed a synthetic spectrum with a kick on
every beat, hot enough to put the sub on its stop 77 times in 7.5 seconds: in the Height view that
crossed the line on every clip. Resolume's own spectrum of a real track is gentler or harder
depending on the track and on **FFT Law**.

**The Lit view never added a flash**: in every run each clip's worst second was the clip's own,
through woofer or not (one of Resolume's clips, Beat 002, reaches the line, 6, on its own). The
cone's motion moves the print about; it doesn't light or blank it. The status lamp's white flash
on a stop hit is a few pixels.

**The Height view is different**: it replaces the picture with the stroke as grey across the
whole driver, so every kick that moves the cone far is a large swing from mid-grey and back. On a
loud master that can be more than three a second. Keep it for setting up, keep Drive down, or keep
it off the audience's screen.

None of this is a substitute for warning your audience. The [browser
demo](https://woofer-demo.stoatworks-labs.com/) opens on the Lit view.

---

## Where the audio comes from

**Audio Input** chooses it. Two ways in:

- **An input, by name.** Woofer drives the cone with that input's waveform, sample by sample, at
  32 kHz: the real thing, with every kick's shape. Inputs are listed by name, and a composition
  remembers the **name**, not the place in the list, so reopened on another day with other things
  plugged in it finds the same device. A device that has gone shows as missing (the lamp blinks
  red) rather than quietly becoming another one, and **Rescan Inputs** lists what is plugged in
  now. **System default** follows the computer's default input. The device is opened off the
  render thread.
- **None (host FFT)**, the default: Resolume's own spectrum, the **Audio** control's, of the
  composition, a layer or an external input as you set it there. **This is much less than an
  input.** The spectrum gives four band levels a frame, with no phase and no waveform, so woofer
  plays one sine per band (63, 400, 1600 and 6400 Hz) at the band's level. The cone still pumps
  on the bass and stays still on the hats, and the motor, the stop and the drift all still act,
  but **it can't show the music's waveform** (two kicks of different pitch look the same), **its
  level isn't calibrated** (**FFT Law** chooses how Resolume's numbers are read, and a band above
  1 is taken as 1), and **it steps once a frame**, so unlike an input it depends on the frame rate.
  **In Arena 7.29.0 on a Mac, it didn't move the cone.** With Arena's own demo clip Beat 001
  playing on the layer below, the cone stayed at rest (under half a per cent of its stroke, the
  Height view's smallest step) under all four **FFT Law**s, the same as with nothing playing,
  in two runs. Whether Arena's spectrum was empty on that Mac's audio set-up, or the levels in
  it were too small to move the cone at +18 dB, wasn't found out. If the cone sits still on
  None (host FFT) with music playing, turn **Drive** up or pick an input.

**Channel** takes the left, the right or both (summed) from an input.

**On macOS the microphone permission belongs to Resolume, not to this plugin.** The first time any
plugin inside Resolume opens an input, macOS asks whether *Resolume* may use the microphone. If
that was refused, macOS hands Resolume silence rather than an error: the lamp goes **steady red**
after 3 s of exact zeros, and the fix is System Settings → Privacy & Security → Microphone →
Resolume Arena. An unpatched Dante receiver or an empty loopback looks the same.

**The status lamp**, a small LED below and right of the driver:

| lamp | means |
| --- | --- |
| green | an input is delivering |
| amber | Resolume's spectrum is driving (None (host FFT), or while a chosen input is down) |
| blinking red | the chosen input is missing or failed; Resolume's spectrum drives meanwhile |
| steady red | the input is open but silent |
| a white flash | the coil hit its stop |

**Status Lamp** turns it off.

---

## The Driver group

| Control | Range | Default | What it does |
| --- | --- | --- | --- |
| **Driver** | 10 cm Full-range, 18 cm Midwoofer, 26 cm Woofer, 30 cm Subwoofer, 18 in PA Sub | 30 cm Subwoofer | Which loudspeaker. The first four are Scan-Speak's (10F/8414G10, 18W/8531G00, 26W/8861T00, 30W/4558T00), the last B&C's 18SW115-16, each set from its datasheet. |
| **Box Qtc** | 0.30–1.50 | 0.707 | The sealed box, as the Q of the driver in it: higher is a smaller box, a stiffer air spring and a peakier bass. At or below the driver's own Qts it's an open baffle. |
| **Xmax** | ×0.25–×2 | ×1 | How far the coil goes before it starts to leave the magnet's gap, against the datasheet's. Lower flat-tops sooner. |
| **Coil Offset** | −0.3 to +0.3 of the gap's height | +0.1 | The coil's rest position off the gap's centre. Any offset makes the motor lopsided, so a loud signal walks the cone's middle off centre. |
| **Breakup** | ×0.25–×4 | ×1 | Moves all four breakup modes up or down in frequency. |

---

## The Audio group

| Control | Range | Default | What it does |
| --- | --- | --- | --- |
| **Audio Input** | None (host FFT), System default, then each input by name | None (host FFT) | Where the audio comes from: see above. |
| **Rescan Inputs** | button | | Lists the inputs again. |
| **Channel** | L+R, Left, Right | L+R | Which channel of an input. |
| **Drive** | −20 to +30 dB | +18 dB | The amplifier, over the voltage at which a full-scale DC input would hold the cone at Xlin, the end of the coil's linear travel. The slider is linear in dB. |
| **FFT Law** | Linear Mag, Linear Power, Log Mag, Log Power | Linear Mag | How the fallback reads Resolume's spectrum: whether its bins are spaced linearly or logarithmically, and whether they hold magnitude or power. Nobody has measured which Resolume sends. |
| **Audio** | Resolume's own | Composition | The spectrum the fallback reads, as on any audio-reactive Resolume effect. |

---

## The Look group

| Control | Range | Default | What it does |
| --- | --- | --- | --- |
| **Size** | 0.3–1.5 × the frame's height | 0.95 | The driver's overall diameter, frame and surround included. |
| **Camera** | 1–5 × the driver's diameter | 1.5 | How far away the pinhole camera is. Closer exaggerates the stroke's swell. The slider is geometric. |
| **Exposure** | 0–40 ms | 8 ms | Motion blur, as a shutter time: the same at any frame rate. |
| **Ripple** | 0–1000 × | 150 × | Display gain on the breakup modes, as a laser vibrometer's plot exaggerates them. At ×1 they're microns. The slider is quadratic. |
| **Light Angle** | 10–90° | 40° | The key light's elevation. |
| **Light Turn** | 0–360° | 135° | The key light's direction round the frame. |
| **Ambient** | 0–1 | 0.2 | The fill light's share. |
| **Gloss** | 0–1 | 0.85 | The specular on the moving parts. |
| **Status Lamp** | Off, On | On | The lamp below and right of the driver. |
| **View** | Lit, Height | Lit | Height shows the stroke as grey instead (mid-grey at rest, lighter toward the camera). It can flash: see [Flashing](#flashing). |
| **Mix** | 0–1 | 1 | With the clip. |

The output is **opaque**: its alpha is the clip's mixed toward 1 by Mix. Where the clip is
transparent the bare driver shows (a dark cone, a grey frame); outside the driver the clip
carries on across the baffle, unlit.

---

## How it works

- **The driver.** A moving coil in a magnet's gap pushes a cone held by a suspension, with the
  box's air behind it. Woofer integrates the coil's current (through its resistance and
  inductance) and the cone's position and speed, at 32 kHz with two Runge–Kutta steps a sample,
  from the voltage: Bl(x) i pushes, the suspension and the air pull back, and the coil's motion
  makes a back voltage. The force factor Bl falls as the coil leaves the gap, the suspension
  stiffens toward the stop, the box's air is an adiabatic spring, and the coil can't pass its
  mechanical limit (Xmech). Four breakup modes ride on top, worked out exactly each sample.
- **Why kicks and not hats.** Below the box's resonance a driver's stroke follows the voltage;
  above it, the stroke falls as 1/f². A 55 Hz kick moves the 30 cm millimetres; a 7 kHz hat at
  the same level moves it microns, 8,559 to 89,030 times less on the five drivers, measured.
- **Independent of the frame rate.** The audio is integrated sample by sample and kept for a
  quarter of a second; each frame draws the stroke at its own time from that record. The same
  audio at 24, 30, 60 and 144 fps gives the same stroke, bit for bit. (Not on the fallback,
  which steps once a frame.)
- **The picture.** The stroke moves the printed clip along the camera's axis, so the pinhole
  magnifies it toward you and shrinks it away. The roll bends at a constant length, so its print
  creases over the crest and, on a full inward stroke, the foot it never reached shows bare. The
  front is lit as a relief: a key light, an ambient share and a glossy specular. The physics runs
  on the CPU in double precision; the GPU only looks up a table of the cone's profile, so no time
  and no maths of the model reach the shader.

---

## Performance

Measured by the offline harness on an Apple Silicon Mac at the defaults, with the harness's
music, medians of 120 frames:

| | 720p | 1080p | 4K |
| --- | --- | --- | --- |
| GPU time a frame, the Mac quiet | 0.38 ms | 0.36 ms | 1.55 ms |
| GPU time a frame, the Mac busy with other builds | 0.93 ms | 1.75 ms | |
| CPU time a frame: the driver | 0.13–0.15 ms | | |
| CPU time a frame: the profile table | about 0.3 ms | | |
| CPU time a frame: the audio analysis | about 0.5–0.6 ms | | |

The CPU side doesn't depend on the raster. About half of it is audio analysis that woofer reads
none of (onsets, pitch, tempo), done every frame on the render thread; there is no switch for it
in this version. Nothing was timed inside Resolume, and nothing was timed on Windows.

---

## If it looks wrong

**The cone doesn't move.** No audio is reaching it. On **None (host FFT)**, check the **Audio**
control's source is playing something; the lamp is amber. In Arena on a Mac, Resolume's spectrum
didn't move the cone even with a clip playing; pick an input instead. On an input, the lamp says why: steady
red is silence (on a Mac, often Resolume's microphone permission), blinking red a device that
isn't there. Or Drive is too low for the track: turn it up.

**It moves on everything, not just the kick.** On the fallback, the high bands are played as
sines at 1600 and 6400 Hz, and a small driver, with its higher resonance, moves more on those than
a big one. Try a bigger driver, or a different **FFT Law**.

**The lamp keeps flashing white.** The coil is hitting its stop: Drive is too high for this
track and this driver.

**The cone sits off centre.** That's **Coil Offset** with a loud signal (the drift), or a small
box's air spring. Coil Offset 0 and a lower Box Qtc keep it centred.

**The picture is grey.** **View** is on **Height**.

**The effect does nothing at all.** That's what a shader that won't compile looks like. The log
has the error, the GL renderer, the inputs it listed and the host clock it settled on:

```
macOS    ~/Library/Logs/woofer/woofer.YYYY-MM-DD.log
Windows  %LOCALAPPDATA%\woofer\logs\woofer.YYYY-MM-DD.log
```

---

## Known limits

- **Run in Resolume only briefly**, on one layer over a still, in Arena 7.29.0 on a Mac and
  7.27.1 on Windows (software rendering, no sound device): it loads, registers and shows its
  controls as declared, and the controls that act on a still cone change the picture. **The cone
  has never moved to music inside Resolume**: Resolume's spectrum didn't move it on the Mac, and
  no input was opened. Nothing is known about a whole show, or how it performs inside Resolume.
- **The Audio Input menu works by value**, as it was built to: Arena reads back and saves the
  chosen entry's value (the device's name key), not its place in the list. Checked with a spare
  "(none)" entry, which opened nothing; a real device was never chosen.
- **No live audio device has been opened**, by woofer or by the audio library inside it, on any
  machine. The device menu, its states and the lamp are proved on a scripted device in the
  harness. Try your input before a show.
- **The fallback is uncalibrated.** Nobody has measured what Resolume's spectrum holds; FFT Law
  is the guess, exposed. A band level above 1 is taken as 1. Under the two Linear laws the bass
  band, the one that plays the 63 Hz sine, reads a single bin of Resolume's spectrum, its lowest.
  It can't show the waveform, and it depends on the frame rate.
- **The audio analysis costs CPU it doesn't use**: about 0.5 ms a frame on the render thread,
  every frame, with no switch to turn it off in this version.
- **Chosen, not measured:** the cone's profile and the dust cap's and surround's shapes, the
  suspension's stiffening (no datasheet gives it), how hard the stop is, the magnet's fringe
  field, the breakup modes (read by eye off the makers' response plots; their shapes are
  assumed), the B&C's Xmech (not printed), and the bare materials' colours.
- **Left out:** vented boxes, the coil's inductance changing with position, heating (thermal
  compression), rocking modes, self-shadowing, and sound: woofer makes none.
- **The defaults were chosen by eye** over seven of Resolume's demo clips with their own stems
  and a mix mastered loud, as stills, not in a show.
- **The Height view can flash** with loud music: see [Flashing](#flashing).
- **No presets** and no OpenFX version.
- **The [browser demo](https://woofer-demo.stoatworks-labs.com/) runs the plugin's own code**,
  compiled to WebAssembly, but with no audio device: it feeds the harness's track, or a file of
  yours decoded in the page, and plays nothing aloud.

---

## About

The last group, **About**, has the plugin's name, version, licence and maker, and buttons that
open the project page, this user guide, the source on GitHub and the support page in your
browser.

## Links

- Project page: [stoatworks-labs.com/software/woofer](https://stoatworks-labs.com/software/woofer/)
- Source: [github.com/stoatworks-labs/woofer](https://github.com/stoatworks-labs/woofer)
- User guide: [stoatworks-labs.com/software/woofer/guide](https://stoatworks-labs.com/software/woofer/guide/)
- Browser demo: [woofer-demo.stoatworks-labs.com](https://woofer-demo.stoatworks-labs.com/)
- The audio front end inside it: [earshot](https://github.com/stoatworks-labs/earshot)

## Reporting something

If something's wrong, shout at
[github.com/stoatworks-labs/woofer/issues](https://github.com/stoatworks-labs/woofer/issues). A
screenshot, the clip, the Audio Input and Driver you used, the settings you changed, and the log
are usually enough.
