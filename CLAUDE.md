# woofer

The clip printed on a loudspeaker cone that the music drives, for Resolume Arena/Avenue, as
one FFGL effect: a real driver's Thiele–Small parameters in a sealed box, voltage-driven by the
audio waveform from earshot and integrated at 32 kHz, Bl(x), a stiffening suspension, an
adiabatic air spring, the stop at Xmech and four breakup modes; the front drawn as a lit
heightfield through a pinhole. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows
`.dll` (never built). MIT.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `WF01` | `SW Woofer` | `Woofer.bundle`, `com.stoatworks.ffgl.woofer` | effect |

Read `AGENTS.md` before touching the integrator (`DriverModel::Run`), Bl(x) and the stop
(`Driver.cpp`), the profile table and the roll (`Profile.cpp`), the fallback
(`AudioInput::Pull`), the device menu (`AudioInput::Refresh`/`Select`) or the clock.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build`. Never from a shared session on this Mac:
  nothing here has been loaded into Arena.
- Render offline: `./build/wftest --out /tmp/w.png` (the defaults on the harness's card, fed
  its track of kicks, hats and a 55 Hz bass through earshot's `feed()`, 3 s in); the summary
  line gives the driver, x now, max |x| against Xmech and the stop hits
- Your own audio: `--wav song.wav` (16/24/32-bit PCM or float), `--silent`, `--left-only`,
  `--host-fft` (nothing fed: Resolume's FFT buffer only, a synthetic kick spectrum)
- List parameters: `./build/wftest --list`
- Set anything by name: `./build/wftest --set "Driver=26 cm Woofer" --set "Drive=0.8"`
  (a standard control takes the host's 0..1; an option takes its name)
- Film: `./build/wftest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - | ./build/wftest --pipe --fps 30 --size WxH --wav in.wav | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`
  (stderr ends with how far the music drove the cone)

## Verify
- Everything: `OXBOW=/path/to/oxbow tools/verify.sh` under bash (GLSL reserved words,
  `glslc`, the submodule pin, the earshot snapshot, a fresh universal build with no warnings
  outside the SDK and the snapshot, lipo, plist, ad-hoc signature, `oxbow probe` and
  `selftest` with `WOOFER_NO_DEVICES=1` -- oxbow is required, never skipped; every check at
  1280x720 and 320x180; six again on Apple's software renderer; `--offline` with no GL
  context; `--pipe` with the track and `--wav`; the negative controls; 17 mutants; the sweep;
  the bench). `KEEP_BUILD=1` reuses `build-verify/`.
- Every check at once: `./build/wftest --all`. One at a time, each at both rasters (`--size WxH`: that raster alone):
  - **Physics, CPU only** (run under `WFTEST_NO_GL=1` by `--offline`): `--smallsignal` (X/V
    against the sealed box's closed form with Le), `--harmonics` (none inside the plateau;
    the air spring's H2 from perturbation theory), `--xmech` (never past Xmech; the Height
    view needs GL), `--framerate` (bit-identical trace at 24/30/60/144 fps), `--pumping`
    (kick against hat), `--flattop` (Bl's fall flattens the peak), `--drift` (the offset
    coil's walk and the box's push), `--surround` (the roll bends, never stretches).
  - **Picture**: `--rest` (silence is the clip), `--magnify` (DC stroke and the pinhole),
    `--alpha` (mix(clip alpha, 1, Mix)), `--resize` (a new raster changes only the drawing).
  - **Audio**: `--fallback` (Resolume's FFT: 63 Hz, the sample count, the amber lamp, a
    millisecond clock), `--devices` (the menu, states and lamp on a scripted backend).
- The checks can fail: `./build/wftest --negative` (19 wrong models; `WFTEST_NEGATIVE=name`
  runs one check's), `tools/mutate.sh` (17 mutants, arm64, 320x180, perl alarm at 300 s).
- No dead controls: `python3 tools/sweep.py` (`--bare` drops the contexts).
- Render cost: `./build/wftest --bench` (720p, 1080p); `--bench4k` adds one 4K run.
- `WFTEST_RENDERER=software` runs any of it on Apple's software renderer.
- **No step opens an audio device.** wftest installs earshot's scripted backend before any
  plugin exists; verify.sh sets `WOOFER_NO_DEVICES=1` for oxbow. Keep it that way: a
  microphone prompt in a test run is a bug.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **The physics is on the CPU in double at the audio rate**: RK4, two substeps per 32 kHz tap
  sample, Catmull–Rom between samples (so the trace runs one sample behind the volts), the
  modes discretised exactly. Every sample's state goes into a 0.256 s ring (the stop is
  enforced at every substep); the picture is drawn from the ring, so it depends on the audio,
  never on how it was chunked into frames.
- **The GPU only looks things up**: the CPU builds a 1024-bin, 16-row profile table per frame
  (rest radius, normal, paint, bare reflectance, lift); the shader indexes it by screen radius.
  No time and no trig reach a shader.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index,
  except Audio Input, whose values are `earshot::deviceKey(name)`.
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed`, `noise1`–`noise4`) must not be identifiers; `verify.sh` greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `woofer_core` is an OBJECT library: the plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- `external/earshot/` is a snapshot of `~/dev/earshot` at `8124191`, unpatched (`SNAPSHOT`).
  Never edit it here; change earshot upstream and take a new snapshot.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide) and
  `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend, `sync-about.py`
  and `sync-attributions.py` generate them; edit them there then.
- **No `.github/workflows/`, on purpose.** GitHub Actions only on a public repo, and this one
  has no remote. Add CI only once it is public.
- Local only: `~/dev/woofer`, no remote, nothing released, not on the website, never loaded
  into Resolume. The spec is `~/Projects/resolume/specs/SPEC-woofer.md`.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/woofer/woofer.YYYY-MM-DD.log
    %LOCALAPPDATA%\woofer\logs\woofer.YYYY-MM-DD.log

`WOOFER_LOG_DIR` overrides the folder.
