# Attributions

Woofer is built on other people's work. This file lists what that work is, who did it, and
what it is doing here.

**A provisional hand copy (2026-11-16).** Woofer is not yet registered in
`stoatworks-backend`, so this was written by hand in the generated file's shape. Once it is
registered, `scripts/sync-attributions.py` generates it from the master lists; edit it there
then, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without
their work.

### Live audio — Stoatworks earshot

<https://github.com/stoatworks-labs/earshot> (intended home; on 2026-11-16 a local repository only)  
Licence: MIT  
Copyright: Stoatworks

Vendored at external/earshot as a snapshot of commit 8124191, unmodified (external/earshot/SNAPSHOT). The capture device worker, the device list and keys, the Silent state, the 32 kHz tap the driver is integrated from, the scripted capture backend the harness drives, and the fallback to Resolume's FFT bins. Woofer is its first consumer. earshot's own attributions (spasis, pattern, miniaudio) are in external/earshot/ATTRIBUTIONS.md.

### The device menu and the microphone-permission explanation — Stoatworks spasis

<https://github.com/stoatworks-labs/spasis>  
Licence: MIT  
Copyright: Stoatworks Labs

The Audio Input option whose values are device-name keys, so a composition remembers the name and never the index; a missing device shown as missing, never swapped for the default; the "(none)" spare slots and Rescan Inputs (source/Audio.cpp, source/Controls.cpp); and the README's account of macOS giving a host silence, not an error, when the microphone is refused. Adapted.

### The harness, the tooling and the plugin shell — Stoatworks fountain, by way of ferrofluid

<https://github.com/stoatworks-labs/fountain> (local only on 2026-11-16)  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, card, cue sheets, --pipe/--film (SIGPIPE ignored, a closed stdout an exit of 1), --negative with its Perturb, --offline (tools/wftest is fttest's infrastructure under new checks); tools/verify.sh, tools/mutate.sh, tools/glslc.sh, tools/sweep.py and scripts/release-lib.sh; CMakeLists.txt's OBJECT library; the clock's vote on seconds or milliseconds (source/Woofer.cpp, ferrofluid's by way of rosette and millpond). Adapted.

### The lit heightfield and the specular — Stoatworks repousse and throw

<https://github.com/stoatworks-labs/repousse>  
<https://github.com/stoatworks-labs/throw>  
Licence: MIT  
Copyright: Stoatworks Labs

A clip lit as a relief under a directional key with an ambient share (repousse); the GGX specular with height-correlated Smith and Schlick's Fresnel, the GLSL conventions (no trig or time in a shader, `blend()`), source/GLState.h and the Controls helpers (throw). Adapted (source/Shaders.cpp).

### The effect shell — Stoatworks slitscan

<https://github.com/stoatworks-labs/slitscan>  
Licence: MIT  
Copyright: Stoatworks Labs

The effect's ProcessOpenGL shape and its input-texture handling. Adapted.

### The log and the About block — Stoatworks millpond and zoetrope

<https://github.com/stoatworks-labs/millpond>  
<https://github.com/stoatworks-labs/zoetrope>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Diag.{h,cpp} is millpond's. source/StoatworksAbout*.h is a provisional hand copy of zoetrope's header, by way of fountain's (no user guide yet).

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned at b1afaf9.

The plugin ABI itself. An FFGL effect is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### miniaudio — David Reid

<https://github.com/mackron/miniaudio>  
Licence: public domain (the Unlicense) or MIT No Attribution, at your choice  
Copyright: David Reid

Arrives inside the earshot snapshot at external/earshot/third_party/miniaudio, version 0.11.25, unmodified. Capture device enumeration and the audio callback.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something this plugin calls directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth
saying out loud.

### The drivers — Scan-Speak and B&C Speakers datasheets

Scan-Speak 10F/8414G10 (updated May 18, 2020), 18W/8531G00 and 26W/8861T00 (updated January 7, 2021), 30W/4558T00 (updated December 7, 2022), from scan-speak.dk; B&C Speakers 18SW115-16 (Rev 0, 10/06/2011), from a distributor's mirror. Each read on 2026-11-16. Re, Le, Bl, Mms, Cms, Rms, Sd, the winding and gap heights, Xlin and Xmech as printed (source/Driver.cpp's preset table); Fs, Qes, Qms, Qts and Vas recomputed from them as the consistency check (the 18W's Vas comes out 11 % above print); the first breakup peak read by eye off each response plot. The datasheets are the makers'; only their numbers are used.

### What the datasheets mean — Scan-Speak Technote 08, "Datasheet explained", Rev 0.1 (18-05-2020)

Linear excursion as half the winding height less the gap height for an overhung coil, which sets Bl(x)'s plateau and Xlin across all five presets; how the maximum mechanical excursion is found; the closed cabinet the response plots were measured in.

### Voice-coil offset — Klippel GmbH, know-how page on nonlinearity curve shapes (read 2026-11-16)

The definition of the coil's offset as the shift of Bl(x)'s symmetry point from rest, and so what Coil Offset means. Nothing else is taken from it; Klippel's AES papers were not read (paywalled).

The lumped equations, the sealed box's transfer function, the adiabatic air spring's second-order term and the drift's sign were derived in the spec from Newton and Kirchhoff, and are checked by the harness against those derivations. R. H. Small's closed-box papers (JAES 20(5), 1972) are the textbook source but were not read (paywalled), and nothing is cited to them.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — toLinear() and toCode() in source/Shaders.cpp: the clip is linearised so it is lit as reflectance, then encoded again.
- **Standard atmosphere, 101 325 Pa, and γ = 1.4 for air** — the sealed box's adiabatic air spring (source/Driver.cpp).

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
