/**
 * Woofer — browser demo.
 *
 * A loudspeaker as an FFGL effect (SW Woofer, WF01): the clip is printed on
 * the front of a real woofer, and the music drives the woofer. Nothing is
 * animated by a parameter: a cone driver set by a real datasheet's
 * Thiele–Small parameters sits in a sealed box and is voltage-driven by the
 * audio waveform, integrated at 32 kHz (RK4, two substeps a sample) with Bl(x),
 * the suspension's stiffening, the air spring, a hard stop at Xmech and four
 * breakup modes, and the stroke moves the printed clip toward and away from a
 * pinhole camera.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `woofer-core.wasm` is
 * the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Woofer.cpp` — the plugin class, with its
 * constructor, its clock and seconds-or-milliseconds vote, the RGBA32F table it
 * uploads every frame and every uniform it sets — and everything it calls: the
 * driver and its integrator (Driver.cpp, in double), the profile table and the
 * roll (Profile.cpp), the audio input (Audio.cpp) and, under it, earshot's
 * snapshot, built with no device backend at all (EARSHOT_CAPTURE=OFF), with
 * the FFGL SDK's CFFGLPlugin, its parameter bookkeeping and FFGLShader. Its GL
 * calls go to this page's WebGL2 context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin (what
 *     PluginEntry.cpp does in a bundle), reads its parameter declarations back
 *     through the SDK's host getters — the panel below is built from them, not
 *     from a list typed here — and forwards SetFloatParameter, SetTime, the
 *     Audio buffer's elements and ProcessOpenGL, as tools/wftest's rig does.
 *   - **The audio.** No device: the page never asks for a microphone and never
 *     lists an input. It feeds the plugin as its harness does, through
 *     earshot's feed() (WooferPlugin::FeedForTest, the path a device's samples
 *     take inside earshot): wftest's own track, generated in the wasm by
 *     wftest's own functions (copied into glue.cpp, held to
 *     tools/wftest/main.cpp by demo/tools/check_shaders.py), or a file the
 *     visitor chooses, decoded here. Or nothing fed, and wftest --host-fft's
 *     synthetic spectrum in the Audio buffer, which is the plugin's own
 *     fallback ("None (host FFT)") running on a made-up FFT.
 *   - **Four GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp by demo/tools/check_shaders.py),
 *     then applies the kit's `port()`. glEnable / glDisable / glIsEnabled:
 *     GL_PROGRAM_POINT_SIZE, which the plugin saves and restores every frame,
 *     does not exist in WebGL2.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's wf_convert); the units, the tooltips
 *     and the presets are this page's words. The presets are written in dB,
 *     metres, degrees and so on and go to the host's 0..1 through the plugin's
 *     own inverse functions (wf_param_from).
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Every audio choice is a new instance, from time 0.** earshot's feed, once
 * begun, is what its engine reads until a selectDevice, and any select ends
 * it; so the page never switches a running instance between sources. Changing
 * the audio restarts the clock, as Restart does, and the new instance's feed
 * starts at the track's beginning.
 *
 * **Each new instance is primed** (mirrorball's rule): five frames about a
 * millisecond apart, so the plugin's seconds-or-milliseconds vote settles
 * before the first frame shown and a paused page stands still. The feed is
 * begun before them, so they take no fallback samples.
 *
 * **Photosensitivity.** The Height view can flash with loud music, the page's
 * own track included (index.html says what was measured). The page opens on
 * the plugin's defaults (the Lit view, which did not); no preset uses the
 * Height view (checked below, at load); a link that opens in it opens paused,
 * with Play to start it; and a caution line under the picture says so
 * whenever View is Height. No default is changed for it.
 *
 * **Left off the panel**: Audio Input and Rescan Inputs (no device exists
 * here), the Audio buffer (Resolume's FFT, which the page writes only in the
 * "None (host FFT)" choice) and the About block, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createWoofer from './woofer-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_TEXT = 100;
const FF_TYPE_BUFFER = 12;

//===========================================================================
// The GLSL: the page's checked copy, assembled as the plugin assembles it
// (ASSEMBLY is read out of AssembleVertex() and AssembleFragment() by
// check_shaders.py), one text per stage.
//===========================================================================

const PAGE_SHADERS = Object.entries(S.ASSEMBLY).map(([name, pieces]) => ({
  name,
  stage: name.endsWith('_vertex') ? 'vertex' : 'fragment',
  text: pieces.map((p) => S[p]).join(''),
}));

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: new Set(), refused: 0 };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies is refused (an
 * empty source, so the plugin's InitGL fails and says so) rather than compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS.filter((s) => s.stage === stage && s.text === text);
  if (!ours.length) {
    shaderCheck.refused += 1;
    return '';
  }
  for (const s of ours) shaderCheck.matched.add(s.name);
  return port(text, { stage });
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createWoofer({
    wooferShaderSource: shaderSource,
    print: (text) => { printed.push(text); console.log(text); },
    printErr: (text) => { printed.push(text); console.warn(text); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/woofer.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

// Before any instance: the plugin's log in the page's memory, and verify.sh's
// switch that keeps AudioInput::Refresh from asking for a device list.
core._wf_prepare();

//===========================================================================
// The declarations, read from the plugin's constructor through the SDK.
//===========================================================================

function declarations() {
  const h = core._wf_new();
  const list = [];
  const n = core._wf_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._wf_param_type(h, i);
    const d = { index: i, name: text(core._wf_param_name(h, i)), type, group: text(core._wf_param_group(h, i)) };
    d.default = type === FF_TYPE_TEXT ? null : core._wf_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._wf_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._wf_param_element_name(h, i, e)));
        d.values.push(core._wf_param_element_value(h, i, e));
      }
    }
    list.push(d);
  }
  const inputs = [core._wf_min_inputs(h), core._wf_max_inputs(h)];
  // Kept, never given GL: the read-outs and the presets ask it for
  // Controls.cpp's conversions.
  return { list, inputs, converter: h };
}

const DECLARED = declarations();
if (DECLARED.inputs.join() !== '1,1') {
  throw new Error(`the plugin declares ${DECLARED.inputs.join('–')} inputs; this page hands it exactly one`);
}

/** Controls.cpp's conversion of a host value. */
const convert = (d, v) => core._wf_convert(DECLARED.converter, d.index, Math.fround(v));
/** And its inverse: the host's 0..1 for a value in the plugin's units. */
const paramFrom = (d, units) => core._wf_param_from(DECLARED.converter, d.index, units);

//===========================================================================
// What the panel leaves out, by the plugin's own names: the device menu and
// its rescan (no device exists here), Resolume's FFT buffer (written only by
// the "None (host FFT)" audio choice) and the About block.
//===========================================================================

const LEFT_OUT = ['Audio Input', 'Rescan Inputs', 'Audio'];
const onPanel = (d) => d.group !== 'About' && !LEFT_OUT.includes(d.name);
for (const name of LEFT_OUT) {
  if (!DECLARED.list.some((d) => d.name === name)) throw new Error(`the plugin no longer declares ${name}; this page's notes say it is left out`);
}

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
const signed = (n, d) => `${n > 0 ? '+' : n < 0 ? '−' : ''}${fixed(Math.abs(n), d)}`;
const DEG = 180 / Math.PI;
// Short: the read-out column is about ten characters wide. The tooltips say
// what each is a multiple of.
const UNITS = {
  'Box Qtc': (x) => fixed(x, 3),
  Xmax: (x) => `×${fixed(x, 2)}`,
  'Coil Offset': (x) => `${signed(x, 2)} gap`,
  Breakup: (x) => `×${fixed(x, 2)}`,
  Drive: (x) => `${signed(x, 1)} dB`,
  Size: (x) => `×${fixed(x, 2)}`,
  Camera: (x) => `×${fixed(x, 2)}`,
  Exposure: (x) => `${fixed(x * 1000, 1)} ms`,
  Ripple: (x) => `${fixed(x, x < 10 ? 1 : 0)}×`,
  'Light Angle': (x) => `${fixed(x * DEG, 0)}°`,
  'Light Turn': (x) => `${fixed(x * DEG, 0)}°`,
};

function readout(d) {
  return (v) => {
    const x = convert(d, v);
    if (Number.isNaN(x)) return d.name === 'Mix' ? `${fixed(v * 100, 0)}%` : fixed(v, 2);
    return (UNITS[d.name] ?? ((y) => fixed(y, 3)))(x);
  };
}

/** Tooltips: this page's words, from the README's description of each control. */
const HINTS = {
  Driver: 'The datasheet the model runs on: a 10 cm full-range, an 18 cm midwoofer, a 26 cm woofer, a 30 cm subwoofer (Scan-Speak) or an 18 in PA sub (B&C). Each is its Thiele–Small parameters, its Xmax and Xmech and its own breakup modes.',
  'Box Qtc': 'The sealed box, as its total Q (Qtc), 0.30–1.50. A smaller box is a stiffer air spring: a higher Qtc and less stroke below resonance. At or below the driver’s own Qts it is an open baffle.',
  Xmax: '×0.25–×2 the datasheet’s coil overhang: how far the coil goes before it starts to leave the gap and the force per amp falls. The slider is geometric, its middle ×1.',
  'Coil Offset': 'The coil’s rest position off the gap’s centre, ±0.3 of the gap’s height (the read-out is in gap heights). Off centre, an overdriven cone drifts toward one side.',
  Breakup: '×0.25–×4 every breakup mode’s frequency. The slider is geometric, its middle ×1.',
  Channel: 'Which channel of the audio drives the cone: L+R (their average), Left or Right. The host row’s “left channel only” track shows it.',
  Drive: 'The amplifier: −20 to +30 dB over the voltage at which full-scale DC holds the cone at Xlin. At the default +18 dB the page’s track puts the 30 cm sub on its stop on the loudest kicks.',
  'FFT Law': 'How the fallback reads Resolume’s FFT bins: linear or log layout, magnitude or power. It acts only on the “None (host FFT)” audio choice in the host row. Arena 7.29.0’s bins were measured as Linear Mag, the default: 64 linear bins of 375 Hz; the other three laws are for hosts nobody has measured.',
  Size: 'The driver’s overall diameter, ×0.3–×1.5 the frame height.',
  Camera: 'The pinhole camera’s distance, ×1–×5 the driver’s diameter: perspective. The slider is geometric. Closer, a cone coming toward the camera magnifies its part of the clip more.',
  Exposure: '0–40 ms of motion blur: the frame is the average of sixteen instants of the cone over the exposure, ending at the newest sample, so it is the same at any frame rate.',
  Ripple: '0–1000 × display gain on the breakup modes, as a laser vibrometer’s plot exaggerates them; at true scale they are microns. The slider is quadratic.',
  'Light Angle': 'The key light’s elevation above the baffle, 10–90°.',
  'Light Turn': 'The key light’s azimuth, 0–360°.',
  Ambient: 'The fill light’s share of the lighting.',
  Gloss: 'Specular highlights on the moving parts.',
  'Status Lamp': 'A small LED below and right of the driver: green, an input is delivering (here, feed()); amber, Resolume’s FFT is driving; steady red, the input is open and silent; a white flash on each hit of the stop.',
  View: 'Lit: the front of the loudspeaker, lit as a heightfield. Height: the axial displacement as grey. The Height view can flash with loud music (the note at the top).',
  Mix: 'With the clip. 0 hands the clip straight back.',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed).
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
};

/** The kit reads these from the query string itself, and this page reads `audio`. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'audio']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_BOOLEAN: return d.default > 0.5 ? 1 : 0;
    default: return d.default;
  }
}

const PARAMS = [];
for (const d of DECLARED.list.filter(onPanel)) {
  if (KIT_TYPES[d.type] === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  const p = { id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: d, default: kitDefault(d) };
  if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
  if (d.type === FF_TYPE_STANDARD) p.display = readout(d);
  PARAMS.push(p);
}
const DEFAULTS = Object.fromEntries(PARAMS.map((p) => [p.id, p.default]));

const query = new URLSearchParams(window.location.search);
const embed = query.has('embed') && query.get('embed') !== '0';

//===========================================================================
// The audio: the stand-in for what reaches the plugin in Resolume.
//===========================================================================

const FEED_RATE = 48000;
const AUDIO = [
  { id: 'track', name: 'wftest’s track (kicks, hats, a 55 Hz bass)', which: 0,
    hint: 'The plugin’s harness’s own music, generated in the WebAssembly by wftest’s own functions: a kick on every beat at 120 BPM, a hat on the off-beat, a 55 Hz bass note two beats in four, peaking near full scale. Fed through earshot’s feed() as a device’s samples would arrive, on both channels.' },
  { id: 'left', name: 'The track on the left channel only', which: 1,
    hint: 'wftest --left-only: the same track on the left, silence on the right. Channel Right then leaves the cone still; L+R halves it.' },
  { id: 'silence', name: 'Silence (zeros fed)', which: 2,
    hint: 'wftest --silent: exact zeros through feed(). After three seconds earshot calls the input Silent and the lamp goes steady red, as it would for an input macOS is handing silence.' },
  { id: 'hostfft', name: 'None (host FFT): a synthetic kick spectrum', which: null,
    hint: 'The plugin’s own default, “None (host FFT)”: nothing is fed, and the plugin’s fallback synthesises its voltage from Resolume’s FFT buffer, four bands a frame. Here that buffer is wftest --host-fft’s made-up kick spectrum, written every frame, not a real FFT of anything. The lamp is amber.' },
  { id: 'file', name: 'Your audio file', which: null,
    hint: 'A file you choose, decoded in this page by the browser and fed through feed(), looped. It is not uploaded and not played aloud.' },
];
const audioById = (id) => AUDIO.find((a) => a.id === id);
const audio = {
  id: AUDIO.some((a) => a.id === query.get('audio') && a.id !== 'file') ? query.get('audio') : 'track',
  file: null, // { name, left: Float32Array, right: Float32Array, seconds }
  epoch: 0, // bumped on every change of audio: a new instance
};

/**
 * Decode a file the visitor chose. An OfflineAudioContext renders to memory,
 * not to an output device; decodeAudioData resamples to its 48 kHz. Nothing
 * here opens a device or asks for one.
 */
async function decodeFile(file) {
  const bytes = await file.arrayBuffer();
  const context = new OfflineAudioContext(2, 1, FEED_RATE);
  const buffer = await context.decodeAudioData(bytes);
  const left = buffer.getChannelData(0);
  const right = buffer.numberOfChannels > 1 ? buffer.getChannelData(1) : left;
  if (!left.length) throw new Error('it holds no samples');
  return { name: file.name, left: new Float32Array(left), right: new Float32Array(right), seconds: left.length / FEED_RATE, channels: buffer.numberOfChannels };
}

//===========================================================================
// Where this page's own measurements found flashing: the Height view.
//===========================================================================

function flashRegion(value) {
  const p = PARAMS.find((x) => x.name === 'View');
  return p.elements[Math.round(value(p.id))] === 'Height';
}

//===========================================================================
// The presets: whole states on top of the constructor's defaults, by the
// plugin's own names. A number in { units } is in the plugin's units (dB, a
// factor, seconds, degrees) and is converted by the plugin's own inverse
// (wf_param_from); an option is its element's name; a bare number is the
// host's 0..1. They are the page's choices, and none is in the Height view
// (checked here).
//===========================================================================

const u = (units) => ({ units });
const PRESET_UNITS = [
  ['The defaults: a 30 cm subwoofer in a Qtc 0.707 box at +18 dB', {}],
  ['10 cm full-range: a small, quick cone whose breakup sits up high', { Driver: '10 cm Full-range', Size: u(0.8) }],
  ['18 in PA sub at +30 dB: flat out, onto its stop on every kick', { Driver: '18 in PA Sub', Drive: u(30) }],
  ['Open baffle: no box, the lowest Qtc', { 'Box Qtc': u(0.3) }],
  ['A small box: Qtc 1.5, a stiff air spring', { 'Box Qtc': u(1.5) }],
  ['The coil off centre: Coil Offset +0.3, the stroke drifts', { 'Coil Offset': u(0.3), Drive: u(24) }],
  ['A short coil: Xmax ×0.25, the peaks flatten', { Xmax: u(0.25) }],
  ['The breakup modes at full Ripple, as a vibrometer plots them', { Ripple: u(1000), 'Light Angle': u(25) }],
  ['Close in: Camera 1×, the cone magnifies the clip', { Camera: u(1), Size: u(1.2) }],
  ['A long exposure: 40 ms of motion blur', { Exposure: u(0.04) }],
  ['Raking light from the left: 15° up', { 'Light Angle': u(15), 'Light Turn': u(180), Ambient: 0.1 }],
];

/**
 * Settings by the plugin's own names as the kit's values: {units} goes
 * through the plugin's ParamFrom*, an option is its element's name, a bare
 * number the host's 0..1. (demo/tools/crosscheck.py uses it too.)
 */
function kitValues(spec, what = 'settings') {
  const values = {};
  for (const [name, value] of Object.entries(spec)) {
    const p = PARAMS.find((x) => x.name === name);
    if (!p) throw new Error(`${what}: no control called ${name}`);
    if (p.type === 'option') {
      const slot = p.elements.indexOf(value);
      if (slot < 0) throw new Error(`${what}: ${name} has no element ${value}`);
      values[p.id] = slot;
    } else {
      const host = typeof value === 'object' ? paramFrom(p.ff, value.units) : value;
      if (!Number.isFinite(host) || host < -1e-6 || host > 1 + 1e-6) throw new Error(`${what}: ${name} ${JSON.stringify(value)} is outside the control`);
      values[p.id] = Math.min(1, Math.max(0, host));
    }
  }
  return values;
}

function kitPresets() {
  const out = {};
  for (const [title, spec] of PRESET_UNITS) {
    const values = kitValues(spec, `preset “${title}”`);
    const all = { ...DEFAULTS, ...values };
    if (flashRegion((id) => all[id])) throw new Error(`preset “${title}” is in the Height view, where this page measured flashing`);
    out[title] = values;
  }
  return out;
}

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

let renderer = null;
let mounted = null;

/** The value the host would send for a kit control, or null for none. */
function hostValue(d, v) {
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT:
    case FF_TYPE_TEXT:
    case FF_TYPE_BUFFER: return null;
    default: return Math.fround(v);
  }
}

function createRenderer(gl) {
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf. The
  // plugin's one float texture (the RGBA32F profile table) is only sampled,
  // with texelFetch, which WebGL2 does without an extension.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);

  let instance = 0;
  let pushed = new Map();
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let instances = 0;
  let lastTime = null;
  let epoch = -1;
  let source = null;
  // The visitor's file, fed as wftest's Feeder feeds a WAV: the absolute
  // sample clock at 48 kHz, samples [0, fileSent) gone to feed(), looped.
  let fileSent = 0;
  let scratch = 0;
  let scratchFrames = 0;

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(width, height) {
    if (instance) core._wf_delete(instance);
    instance = core._wf_new();
    pushed = new Map();
    frames = 0;
    instances += 1;
    fileSent = 0;
    source = audio.file || audio.id !== 'file' ? audio.id : 'track';
    if (!core._wf_init_gl(instance, width, height)) {
      const why = shaderCheck.refused > 0
        ? 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled'
        : `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`SW Woofer could not start: ${why}.`);
    }
    // Everything but the host FFT goes through feed() from here on: earshot's
    // feedBegin(48000, 2), its state Running, as wftest's Feeder does it.
    if (source !== 'hostfft') core._wf_feed_begin(instance);
  }

  function push(params) {
    for (const p of PARAMS) {
      const value = hostValue(p.ff, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._wf_set_float(instance, p.ff.index, value);
    }
  }

  function feedFile(end) {
    const f = audio.file;
    const n = Math.min(Math.max(0, Math.round(end) - fileSent), 4 * FEED_RATE);
    if (n <= 0) return;
    if (n > scratchFrames) {
      if (scratch) core._free(scratch);
      scratchFrames = Math.max(n, FEED_RATE);
      scratch = core._malloc(scratchFrames * 2 * 4);
    }
    const out = core.HEAPF32.subarray(scratch >> 2, (scratch >> 2) + 2 * n);
    const length = f.left.length;
    for (let k = 0; k < n; k += 1) {
      const at = (fileSent + k) % length;
      out[2 * k] = f.left[at];
      out[2 * k + 1] = f.right[at];
    }
    core._wf_feed(instance, scratch, n, FEED_RATE, 2);
    fileSent += n;
  }

  /** The audio for a frame at page time `time`, before the plugin renders it. */
  function feed(time) {
    if (source === 'hostfft') {
      core._wf_host_fft(instance, time);
    } else if (source === 'file') {
      feedFile(time * FEED_RATE);
    } else {
      core._wf_feed_track(instance, time * FEED_RATE, audioById(source).which);
    }
  }

  function process(time, input) {
    if (!core._wf_process(instance, time, glName(input.texture), input.width, input.height)) {
      throw new GLError('The plugin’s ProcessOpenGL failed.');
    }
  }

  /**
   * Five host frames about a millisecond apart, before the first one shown:
   * enough for the plugin's seconds-or-milliseconds vote (four frames that
   * agree against the wall clock) to settle, so a paused page stands still.
   * Nothing is fed during them (the host FFT's spectrum is written for each).
   * The clock then steps back to the page's time, which the plugin takes as
   * no time passing.
   */
  function prime(time, input) {
    const t0 = performance.now();
    for (let k = 0; k < 5; k += 1) {
      if (k > 0) {
        // The plugin needs 0.5 ms of wall clock between votes. Date.now() caps
        // the wait where performance.now() stands still (a test harness's
        // stepped clock).
        const until = performance.now() + 1.05;
        const cap = Date.now() + 20;
        while (performance.now() < until && Date.now() < cap) { /* wait */ }
      }
      const at = time + (performance.now() - t0) / 1000;
      if (source === 'hostfft') core._wf_host_fft(instance, at);
      process(at, input);
    }
  }

  return {
    render({ input, params, width, height, time }) {
      core.GL.makeContextCurrent(handle);
      // A new instance for a change of audio, and for Restart (the page's
      // clock going back).
      const fresh = !instance || epoch !== audio.epoch || (lastTime !== null && time < lastTime);
      lastTime = time;
      epoch = audio.epoch;
      if (fresh) start(width, height);
      push(params);
      if (fresh) prime(time, input);

      feed(time);
      process(time, input);

      // A setter WebGL2 refuses (a float sent to an int, say) fails silently
      // but for this: asked on an instance's first frames only, as a sync
      // point is not free. demo/tools/check_shaders.py checks the setters
      // statically.
      frames += 1;
      if (frames <= 3) {
        const error = gl.getError();
        if (error !== gl.NO_ERROR) {
          gl.canvas.dataset.glError = `0x${error.toString(16)}`;
          console.error(`WebGL reported error 0x${error.toString(16)} on frame ${frames} of SW Woofer`);
        }
      }
      // For anything checking the page from outside.
      gl.canvas.dataset.frames = String(frames);
      gl.canvas.dataset.instances = String(instances);
      gl.canvas.dataset.audio = source;
      gl.canvas.dataset.x = String(core._wf_x_now(instance));
      gl.canvas.dataset.hits = String(core._wf_stop_hits(instance));
    },
    stats() {
      if (!instance) return null;
      return {
        source,
        model: text(core._wf_driver_model(instance)),
        x: core._wf_x_now(instance),
        maxAbsX: core._wf_max_abs_x(instance),
        xmech: core._wf_xmech(instance),
        xlin: core._wf_xlin(instance),
        hits: core._wf_stop_hits(instance),
        count: core._wf_count(instance),
        samples: core._wf_samples_last_frame(instance),
        fromDevice: core._wf_from_device(instance) === 1,
        state: text(core._wf_capture_state_name(instance)),
        now: core._wf_host_now(instance),
        clockScale: core._wf_clock_scale(instance),
        fed: source === 'file' ? fileSent : core._wf_fed(instance),
        frames,
      };
    },
    log() {
      try {
        return core.FS.readFile(text(core._wf_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
    shaders: shaderCheck,
  };
}

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================

const DIFFERENCES = [
  'This page runs the plugin rather than a port of it. woofer-core.wasm is source/Woofer.cpp — the plugin class: its constructor, its clock and seconds-or-milliseconds vote, the profile table it uploads every frame, every uniform and the GL state it saves and puts back — and everything it calls (Driver, Profile, Audio, Controls, Shaders, Diag), with earshot’s snapshot (02306da, unpatched) and the FFGL SDK’s CFFGLPlugin, parameter bookkeeping and FFGLShader, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). The driver — RK4 at 32 kHz in double, Bl(x), the suspension, the air spring, the stop and the breakup modes — is that C++. Only the registration (PluginEntry.cpp) is left out; the page constructs the plugin itself, as its harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
  'No audio device, and no microphone: the page never asks for one, never lists one and makes no sound. earshot is compiled here with no capture backend at all (EARSHOT_CAPTURE=OFF: its NoCapture.cpp, and miniaudio is not compiled), and the plugin is told not to ask for a device list (WOOFER_NO_DEVICES=1, its verification switch). So three of the plugin’s controls are not on this panel: Audio Input (the device menu: None (host FFT), System default, or a device by name), Rescan Inputs, and the Audio buffer (Resolume’s FFT, which a host writes and nobody sets). The live-device path — a device opened by earshot’s worker thread, its 32 kHz tap, Missing and Failed, the blinking red lamp, the remembered device name — is not in the browser.',
  'The audio is the plugin’s harness’s, handed over as its harness hands it: through earshot’s feed() (WooferPlugin::FeedForTest), the path a device’s samples take inside earshot, so the lamp is green and the cone is driven by the waveform, sample by sample. The track is wftest’s own (a kick on each beat at 120 BPM, a hat on the off-beat, a 55 Hz bass two beats in four), generated in the WebAssembly by wftest’s functions, copied character for character and held to tools/wftest by the repository’s verify script; it is fed up to the page’s clock each frame, at 48 kHz, on both channels or the left alone, or as exact zeros. Your own audio file is decoded by this browser (Web Audio’s decodeAudioData, in an OfflineAudioContext, resampled to 48 kHz; a mono file goes to both channels), never uploaded, and fed the same way, looped; wftest --wav plays a file once and then feeds silence. Nothing is played aloud.',
  '“None (host FFT)”, the plugin’s own default Audio Input, is offered in the host row: nothing is fed and the plugin’s fallback synthesises its voltage from Resolume’s FFT buffer (a sine per band at 63, 400, 1600 and 6400 Hz, its level stepping once a frame). Here the buffer holds wftest --host-fft’s synthetic kick spectrum, written every frame from the page’s clock — a made-up FFT, not an analysis of any sound. In Resolume the buffer is Resolume’s own FFT: in Arena 7.29.0, measured, 64 linear bins of 375 Hz holding magnitudes (FFT Law’s default, Linear Mag), and earshot shares each bin among the bands it overlaps, so the 20–200 Hz band (the 63 Hz sine) takes 180 Hz of the lowest bin’s 375 (in v0.1.0 it took all of it). Levels are still relative: the fallback is uncalibrated.',
  'Every change of audio is a new instance from time 0, as Restart is: a feed, once begun, is what earshot reads until a device is selected, and the page never selects one. The page’s default audio is the track, as if a device were chosen; the plugin’s default, with no device, is “None (host FFT)”.',
  'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order AssembleVertex() and AssembleFragment() join the pieces in, and the repository’s verify script fails if a character differs, or if a uniform is set by a call its GLSL type does not take. At start-up the page also requires both stages the compiled plugin hands to glShaderSource to equal its copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults. GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and four entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port, and glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE — state the plugin saves and restores each frame, as a plugin in Resolume must — does not exist in WebGL2. It reads as off here; the plugin draws no points.',
  'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame, handed to SetTime, and the audio is fed up to it. The plugin votes against the wall clock on whether those are seconds or milliseconds; so that a paused page stands still, each new instance — page load, a change of audio and Restart — is handed five frames about a millisecond apart before the first one shown, which settles the vote, with nothing fed during them. Paused, a moved control redraws the frame without feeding or moving the cone. Step advances the clock 1/60 s, which feeds 800 samples.',
  'A frame here is one of the display’s refreshes (60 or 120 a second, and whenever the browser gets round to it), not Resolume’s output frame. On the feed() path the cone does not depend on it: every sample is integrated once, and wftest measures the trace bit-identical at 24, 30, 60 and 144 fps. Which instants are drawn does; and on “None (host FFT)” the fallback steps once a frame, so there it does depend on the frame rate, as in Resolume.',
  'WebAssembly is not the Mac’s arm64. It has no fused multiply-add, and its sin, cos, exp and pow are musl’s, not Apple’s, so the integrator’s doubles can differ from the plugin built for a Mac in the last digits, and an integrator that hits a hard stop can carry such a difference further. What reaches the GPU is the profile table as float, as in the plugin, rasterised by this browser’s GPU, not the Mac’s.',
  'The clip is the kit’s generated one, or your own image or video. The kit hands over premultiplied colour, and the plugin treats its input as straight colour with alpha; the page’s own clips are all opaque, which is why the kit’s shape-on-transparency clip is not among them, and an image of your own with transparency would show the difference at its edges. In Arena the effect gets the composition’s own frame; here its whole texture is used, unpadded.',
  'The photosensitivity note at the top, the caution line under the picture and the rule that a link opening in the Height view opens paused are the page’s, not the plugin’s; in embed mode (?embed=1, the output as a video source, with no Play button to press) neither the note nor that rule applies. No default was changed for them. The About block (a credit line and its buttons) is declared by the plugin and left off this panel.',
  'Nothing here is measured. The plugin has run in Resolume only briefly, where Resolume’s own FFT moved the cone a little to a demo drum loop, and has never opened a live audio device; its bundle is also read and rendered through by a host-shaped tool (its README’s Status says what was seen). Its harness, wftest, drives the same class headlessly on a synthetic clock: the cone’s stroke matches the sealed box’s closed-form transfer function to 3.2e-4 over 24 sines on all five drivers, the coil never passes Xmech at any substep, the trace is bit-identical across frame rates, and 19 wrong models and 17 one-character mutants are caught. That harness, not this page, is the reason to believe the cone. What the cone looks like beyond the datasheets’ numbers — its profile, its breakup modes, how hard the stop is — is chosen, not measured.',
];

//===========================================================================
// The page.
//===========================================================================

mounted = mountDemo({
  name: 'Woofer',
  pluginId: 'WF01',
  kind: 'effect',
  tagline:
    'A loudspeaker for Resolume: your clip is printed on the front of a real woofer, and the music drives the woofer. Nothing is animated by a parameter. A cone driver set by a real datasheet sits in a sealed box and is voltage-driven by the audio waveform, integrated at 32 kHz, so kicks pump the cone and hi-hats leave it still, a big hit flat-tops as the coil leaves the gap, the coil bottoms on its stop, an overdriven cone drifts, and the printed clip magnifies, stretches and creases as the cone comes toward the camera.',
  repo: 'https://github.com/stoatworks-labs/woofer',

  blurb:
    'It is the plugin’s own C++ — the plugin class, the driver integrated in double at 32 kHz, the profile table, its audio input and earshot under it — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. The host is this page: the audio is the plugin’s harness’s own track, or a file of yours, fed through earshot’s feed() as a device’s samples would be, never a microphone; the clip is a generated one.',

  needFloat: false,
  // The output is opaque at Mix 1, and the page's clips are opaque.
  showBackdrop: false,

  // All opaque: the kit's 'alpha' clip is left out because the kit's
  // premultiplied colour is not the straight colour the plugin expects (see
  // the notes at the foot).
  sources: ['scene', 'bars', 'grid', 'detail', 'spot', 'ramp'],

  params: PARAMS,
  presets: kitPresets(),
  differences: DIFFERENCES,

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// The open-paused rule, the host row and the lines under the picture.
//===========================================================================

if (mounted) {
  const params = mounted.params;

  // Copy link carries the audio choice (a file cannot travel in a link).
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (audio.id !== 'track' && audio.id !== 'file') q.set('audio', audio.id);
    return q;
  };

  // A link that opens in the Height view opens paused.
  if (!embed && flashRegion((id) => params.get(id))) {
    mounted.state.playing = false;
    const play = document.querySelector('.btn--play');
    if (play) play.textContent = 'Play';
  }

  if (!embed) {
    // The kit's note says every declared parameter is here; four are not.
    const note = document.querySelector('.inspector__note');
    if (note) {
      note.textContent += ' Left out here: Audio Input and Rescan Inputs (no audio device exists in the browser), the Audio buffer (Resolume’s FFT; the host row under the picture stands in for all three) and the About block.';
    }
    buildHostRow();
    buildStatusLine();
  }
}

//---------------------------------------------------------------------------
// The host row: the stand-in for what Resolume would hand the plugin.
//---------------------------------------------------------------------------
function field(label, control, title) {
  const wrap = document.createElement('label');
  wrap.className = 'transport__field';
  if (title) wrap.title = title;
  const span = document.createElement('span');
  span.className = 'transport__label';
  span.textContent = label;
  // Narrow screens: the field may shrink, and the control with it.
  wrap.style.maxWidth = '100%';
  wrap.style.minWidth = '0';
  control.style.maxWidth = '100%';
  control.style.minWidth = '0';
  wrap.append(span, control);
  return wrap;
}

/** Restart the page's clock: a new instance from time 0, as Restart does. */
function restartClock() {
  audio.epoch += 1;
  mounted.state.time = 0;
  mounted.state.frameIndex = 0;
  mounted.redraw();
}

function buildHostRow() {
  const transport = document.querySelector('.transport');
  if (!transport) return;
  const row = document.createElement('div');
  row.className = 'transport';
  row.id = 'woofer-host';
  const tag = document.createElement('span');
  tag.className = 'transport__label';
  tag.textContent = 'The host:';
  tag.title = 'The stand-in for what Resolume would hand the plugin. Not a plugin control.';

  const select = document.createElement('select');
  select.className = 'transport__select';
  select.id = 'woofer-audio';
  for (const a of AUDIO) {
    const option = new Option(a.name, a.id);
    option.title = a.hint;
    if (a.id === 'file') option.disabled = true;
    select.append(option);
  }
  select.value = audio.id;
  select.addEventListener('change', () => {
    audio.id = select.value;
    restartClock();
  });

  const pick = document.createElement('label');
  pick.className = 'btn transport__file';
  pick.id = 'woofer-audio-file-label';
  pick.textContent = 'Audio file…';
  pick.title = 'Choose a sound file on this machine. The browser decodes it here and the page feeds it to the plugin through feed(); it is not uploaded and not played aloud.';
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = 'audio/*';
  input.id = 'woofer-audio-file';
  input.hidden = true;
  pick.append(input);
  const note = document.createElement('span');
  note.className = 'transport__label';
  note.id = 'woofer-audio-note';
  note.setAttribute('role', 'status');
  input.addEventListener('change', async () => {
    const file = input.files?.[0];
    if (!file) return;
    note.textContent = `Decoding ${file.name}…`;
    try {
      audio.file = await decodeFile(file);
      const fileOption = [...select.options].find((o) => o.value === 'file');
      fileOption.disabled = false;
      fileOption.textContent = `Your file: ${file.name}`;
      audio.id = 'file';
      select.value = 'file';
      note.textContent = `${file.name}: ${audio.file.seconds.toFixed(1)} s, ${audio.file.channels === 1 ? 'mono, on both channels' : `${audio.file.channels} channels, the first two`}, at 48 kHz, looped. It stays in this page.`;
      restartClock();
    } catch (error) {
      note.textContent = `That file could not be decoded: ${error.message ?? error}`;
    }
  });

  row.append(
    tag,
    field('Audio', select, 'What reaches the plugin in place of an audio input. The plugin’s Audio Input menu, Rescan Inputs and the live-device path are not in the browser: the page feeds earshot’s feed() as the plugin’s harness does. Changing it restarts the clock (a new instance).'),
    pick,
    note,
  );
  transport.after(row);
}

//---------------------------------------------------------------------------
// The lines under the picture: the plugin's own state, through the accessors
// its harness reads, and the caution when the panel is in the Height view.
// They report; they measure nothing.
//---------------------------------------------------------------------------
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const line = document.createElement('p');
  line.className = 'stage__status wf-status';
  line.id = 'woofer-status';
  line.setAttribute('aria-live', 'off');
  const caution = document.createElement('p');
  caution.className = 'stage__status wf-caution';
  caution.id = 'woofer-caution';
  caution.setAttribute('aria-live', 'polite');
  caution.hidden = true;
  const log = document.createElement('p');
  log.className = 'stage__status wf-log';
  log.id = 'woofer-log';
  log.setAttribute('aria-live', 'off');
  stage.append(line, caution, log);

  const params = mounted.params;
  const mm = (m) => `${signed(m * 1000, 1)} mm`;

  let lastLine = '';
  let lastLog = '';
  setInterval(() => {
    const s = renderer?.stats();
    if (!s) return;
    let feedText;
    if (s.source === 'hostfft') {
      feedText = `nothing fed: the fallback on wftest --host-fft’s synthetic spectrum (earshot ${s.state}, the lamp amber)`;
    } else {
      const lamp = s.state === 'Silent' ? 'steady red: silent' : s.fromDevice ? 'green' : 'amber';
      const what = s.source === 'file' ? 'your file' : s.source === 'silence' ? 'zeros' : s.source === 'left' ? 'the track (left channel only)' : 'the track';
      feedText = `${what} through feed(), ${(s.fed / FEED_RATE).toFixed(2)} s so far (earshot ${s.state}, the lamp ${lamp})`;
    }
    const next = `${s.model}: the coil at ${mm(s.x)} (Xmax ${fixed(s.xlin * 1000, 1)} mm, its stop at ${fixed(s.xmech * 1000, 1)} mm); since this instance started, at most ${fixed(s.maxAbsX * 1000, 1)} mm and ${s.hits.toLocaleString('en-GB')} hit${s.hits === 1 ? '' : 's'} of the stop`
      + ` · ${feedText} · ${s.samples} samples at 32 kHz integrated this frame`
      + ` · the plugin’s clock at ${s.now.toFixed(2)} s${s.clockScale === 1 ? '' : ' (still voting)'}`;
    if (next !== lastLine) {
      lastLine = next;
      line.textContent = next;
    }

    const flashing = flashRegion((id) => params.get(id));
    caution.hidden = !flashing;
    caution.textContent = flashing
      ? 'The Height view can flash: it shows the cone’s stroke as grey over the whole picture, and on this page’s own track it went past three flashes a second at the default Drive. Flashing light can trigger seizures in people with photosensitive epilepsy. Pause stops it at once.'
      : '';

    const textLog = renderer.log();
    if (textLog === lastLog) return;
    lastLog = textLog;
    const lines = textLog.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) woofer: /, '$1 '));
    log.textContent = lines.length ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}` : '';
  }, 250);
}

// For checking the page from outside (the crosscheck against wftest reads the
// declarations and the inverse conversions through these).
export { renderer, mounted, PARAMS, DECLARED, DEFAULTS, core, flashRegion, hostValue, kitValues, audio, restartClock };
