# Attributions

Woofer is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### earshot — Stoatworks earshot

<https://github.com/stoatworks-labs/earshot>  
Licence: MIT  
Copyright: Stoatworks Labs

external/earshot/ is earshot, the fleet's shared audio front end, as a snapshot: one commit taken with git archive and left unmodified, named in external/earshot/SNAPSHOT. It is built into the plugin by add_subdirectory and supplies the capture device, chosen by name with a Silent state, and the audio analysis the plugin reads every frame. It brings miniaudio with it, credited below.

### The device menu and the microphone-permission explanation — Stoatworks spasis

<https://github.com/stoatworks-labs/spasis>  
Licence: MIT  
Copyright: Stoatworks Labs

The Audio Input option whose values are device-name keys, so a composition remembers the name and never the index; a missing device shown as missing, never swapped for the default; the "(none)" spare slots and Rescan Inputs (source/Audio.cpp, source/Controls.cpp); and the README's account of macOS giving a host silence, not an error, when the microphone is refused. Adapted.

### The harness, the tooling and the plugin shell — Stoatworks fountain, and ferrofluid before it

<https://github.com/stoatworks-labs/fountain>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, card, cue sheets, --pipe/--film (SIGPIPE ignored, a closed stdout an exit of 1), --negative with its perturbation hooks and --offline (tools/wftest is fttest's infrastructure under new checks); tools/verify.sh, tools/mutate.sh, tools/glslc.sh and tools/sweep.py; CMakeLists.txt's OBJECT library; the clock's vote on seconds or milliseconds (source/Woofer.cpp, ferrofluid's by way of rosette and millpond). Adapted. scripts/release-lib.sh is the fleet's, unchanged.

### The lit relief — Stoatworks repousse

<https://github.com/stoatworks-labs/repousse>  
Licence: MIT  
Copyright: Stoatworks Labs

A clip lit as a relief under a directional key with an ambient share (source/Shaders.cpp). Adapted.

### The specular and the GL conventions — Stoatworks throw

<https://github.com/stoatworks-labs/throw>  
Licence: MIT  
Copyright: Stoatworks Labs

The GGX specular with height-correlated Smith and Schlick's Fresnel, the GLSL conventions (no trig or time in a shader, blend()), source/GLState.h and the Controls helpers. Adapted (source/Shaders.cpp).

### The effect shell — Stoatworks slitscan

<https://github.com/stoatworks-labs/slitscan>  
Licence: MIT  
Copyright: Stoatworks Labs

The effect's ProcessOpenGL shape and its input-texture handling. Adapted.

### The log — Stoatworks millpond

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Diag.{h,cpp} is millpond's.

### The release workflow and the DLL check — Stoatworks mirrorball, filings and pinhole

<https://github.com/stoatworks-labs/mirrorball>  
Licence: MIT  
Copyright: Stoatworks Labs

.github/workflows/ci.yml is mirrorball's and release.yml is filings' (both xenon's line): the universal bundle and the Windows DLL, ATTRIBUTIONS.md in each zip, the Windows installer per user into Resolume's Extra Effects. tools/wincheck.ps1, which loads the DLL as a host loads it and reads back its name, id, type and parameters, is mirrorball's (pinhole's, penumbra's, pepper's). Adapted.

### The browser demo's build and kit — Stoatworks mirrorball and fountain, and polyhedral before them

<https://github.com/stoatworks-labs/mirrorball>  
Licence: MIT  
Copyright: Stoatworks Labs

demo/tools/build-wasm.sh, which compiles the plugin's own C++ and the earshot snapshot to WebAssembly unmodified, demo/wasm/glue.cpp (the page as the host), demo/wasm/gl_shim.cpp, demo/tools/check_shaders.py and demo/tools/crosscheck.py are mirrorball's and fountain's, polyhedral's pattern; the page around them is the fleet's shared demo kit (demo/vendor/). Adapted.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

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

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

### miniaudio, inside earshot

<https://github.com/mackron/miniaudio>  
Licence: Public Domain (Unlicense) or MIT-0, at your choice  
Copyright: David Reid

Arrives inside the vendored earshot snapshot, at external/earshot/third_party/miniaudio/miniaudio.h, unmodified. Not vendored separately.

earshot's capture backend: it lists the capture devices and runs the audio callback, on CoreAudio and WASAPI. The plugin never calls miniaudio itself; it arrives only with earshot.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### The drivers' datasheets — Scan-Speak; B&C Speakers

<https://www.scan-speak.dk/>

Scan-Speak 10F/8414G10 (updated May 18, 2020), 18W/8531G00 and 26W/8861T00 (updated January 7, 2021), 30W/4558T00 (updated December 7, 2022), from scan-speak.dk; B&C Speakers 18SW115-16 (Rev 0, 10/06/2011), from a distributor's mirror. Each read on 2026-11-16. Re, Le, Bl, Mms, Cms, Rms, Sd, the winding and gap heights, Xlin and Xmech as printed (source/Driver.cpp's preset table); Fs, Qes, Qms, Qts and Vas recomputed from them as the consistency check (the 18W's Vas comes out 11 % above print); the first breakup peak read by eye off each response plot. The datasheets are the makers'; only their numbers are used.

### What the datasheets mean — Scan-Speak

Technote 08, "Datasheet explained", Rev 0.1 (18-05-2020): linear excursion as half the winding height less the gap height for an overhung coil, which sets Bl(x)'s plateau and Xlin across all five presets; how the maximum mechanical excursion is found; the closed cabinet the response plots were measured in.

### Voice-coil offset — Klippel GmbH

The know-how page on nonlinearity curve shapes (read 2026-11-16): the definition of the coil's offset as the shift of Bl(x)'s symmetry point from rest, and so what Coil Offset means. Nothing else is taken from it; Klippel's AES papers were not read (paywalled).

### The lumped model, derived

The lumped equations, the sealed box's transfer function, the adiabatic air spring's second-order term and the drift's sign were derived in the spec from Newton and Kirchhoff, and wftest checks the plugin against those derivations. R. H. Small's closed-box papers (JAES 20(5), 1972) are the textbook source but were not read (paywalled), and nothing is cited to them.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### The subwoofer you can see move

A bare driver at a gig, its cone pumping on the kick and still on the hats. Built from how a driver works, from its datasheet; nothing is copied from anyone's software or visualiser.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — toLinear() and toCode() in source/Shaders.cpp: the clip is linearised so it is lit as reflectance, then encoded again.
- **The standard atmosphere, and γ = 1.4 for air** — 101 325 Pa and the ratio of specific heats: the sealed box's adiabatic air spring (source/Driver.cpp).

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
