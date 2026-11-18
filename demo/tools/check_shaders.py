"""The demo's copies of the plugin must be the plugin, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.
(mirrorball's script, which is pinhole's and fountain's, with woofer's pieces,
its two-function assembly, its `glUniform*( where( "Name" ), ... )` calls,
wftest's track and earshot's sources.)

------------------------------------------------------------------- why

The page at woofer-demo.stoatworks-labs.com carries copies of this repository,
and copies drift quietly: a cone lit with the wrong normal, or a track whose
kick sits an octave up, still looks like a loudspeaker.

1. **The GLSL.** `demo/shaders.js` holds the three GLSL pieces of
   `source/Shaders.cpp` (kQuadVertex, kCommon, kWooferFragment) plus kVersion,
   AND the order the plugin joins them in: Shaders.cpp's AssembleVertex() and
   AssembleFragment(), which Woofer.cpp's InitGL compiles as its one program.
   This script reads that order out of the C++ and writes it into shaders.js as
   `ASSEMBLY`. The page builds its expected text for both stages from that
   table, and at run time REQUIRES the text the compiled plugin hands to
   glShaderSource to equal it before the kit's port() is applied
   (demo/wasm/gl_shim.cpp). The pieces are compared exactly -- no whitespace
   normalisation, no comment stripping: a comment updated on one side only is
   drift worth catching. Every `const char* const k... = R"(` piece in
   Shaders.cpp must be one this script knows, and shaders.js must be byte for
   byte what `--write` produces, so nothing else can be typed into it.

   The one transformation is a decode: a backtick cannot sit raw in a template
   literal, so shaders.js would escape it as \\`. Any other backslash, or a
   `${`, on the JS side is rejected; the C++ bodies hold neither.

2. **Every uniform's setter.** The page runs the plugin's own glUniform calls,
   and WebGL2 is stricter about them than a desktop driver is allowed to be
   lenient: a float sent to an `int` or a sampler is INVALID_OPERATION and the
   uniform stays 0, silently, and a sampler nobody sets sits on unit 0. Woofer.cpp
   sets every uniform as `glUniformNx( where( "Name" ), ... )`; each uniform the
   program declares must be set by the call its type takes, and nothing set that
   it does not declare.

3. **wftest's track.** The page feeds the plugin wftest's own music through
   earshot's feed(). demo/wasm/glue.cpp carries tools/wftest/main.cpp's
   `fadeIn`, `kickAt`, `hatSamples`, `hatAt`, `kickPeak` and `musicAt`, with
   the lines they need (kFeedRate, Floats, Doubles), and the --host-fft
   spectrum's loop, formula and setter lines (tools/wftest/run.inc, with
   `instance->plugin` for wftest's `rig.plugin`); each must be wftest's text
   character for character.

4. **No device.** No file the page serves may name getUserMedia,
   enumerateDevices or mediaDevices: the page never asks for a microphone or
   lists an audio device, and this keeps it so.

5. **The WebAssembly.** `demo/woofer-core.wasm` is the plugin's C++ and
   earshot's compiled by `demo/tools/build-wasm.sh`, and committed, because the
   deploy has no build step. Building it needs emscripten, which verify.sh
   cannot assume, so this checks the next best thing: `demo/wasm/inputs.sha256`,
   written by the build, records the hash of every file that went in (every
   source/*.h, the seven plugin .cpp files, earshot's sources and headers, the
   SDK files, the glue, the shim and the build script), of both outputs, and the
   SDK commit. A source file changed since the last build -- a regenerated
   StoatworksAbout.h or a new earshot snapshot included -- or an output that is
   not the one the build wrote fails here, and the fix is to rerun
   build-wasm.sh.

------------------------------------------------------------------- what it cannot

It cannot tell whether the .wasm really is what those inputs compile to (only
rebuilding can), and it says nothing about demo/plugin.js beyond rule 4: the
panel is built from the plugin's own declarations at run time, but its read-out
units, its tooltips, its presets and its layout are the page's.
"""
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there. kVersion is a plain string and is handled on its own.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("COMMON", "kCommon"),
    ("WOOFER_FRAGMENT", "kWooferFragment"),
]
JS_NAME = dict((symbol, name) for name, symbol in SHADERS)
JS_NAME["kVersion"] = "VERSION"

# The setters each GLSL uniform type takes, as Woofer.cpp spells them.
SETTER = {
    "sampler2D": {"glUniform1i"},
    "int": {"glUniform1i"},
    "bool": {"glUniform1i"},
    "ivec2": {"glUniform2i"},
    "float": {"glUniform1f"},
    "vec2": {"glUniform2f", "glUniform2fv"},
    "vec3": {"glUniform3f", "glUniform3fv"},
    "vec4": {"glUniform4f", "glUniform4fv"},
}

# wftest's track, copied into the glue.
HARNESS = ["fadeIn", "kickAt", "hatSamples", "hatAt", "kickPeak", "musicAt"]
HARNESS_LINES = [
    "constexpr int kFeedRate = 48000;",
    "using Floats  = std::vector< float >;",
    "using Doubles = std::vector< double >;",
    # The --host-fft spectrum, in tools/wftest/run.inc.
    "for( int e = 0; e < 64; ++e )",
    "const double v = std::exp( -t / 0.12 ) * 0.8 * std::exp( -e / 6.0 ) + 0.05 * std::exp( -e / 30.0 );",
    "rig.plugin.SetParamElementValue( PT_AUDIO, static_cast< unsigned int >( e ), static_cast< float >( v ) );",
]

# What a page that never touches a device must never name.
DEVICE_APIS = ("getUserMedia", "enumerateDevices", "mediaDevices")

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape is \\` for a backtick inside a comment.
"""

MANIFEST = os.path.join("demo", "wasm", "inputs.sha256")
OUTPUTS = ("demo/woofer-core.js", "demo/woofer-core.wasm")
REGISTRATION = ("PluginEntry.cpp",)
# earshot's device backend, which build-wasm.sh leaves out (EARSHOT_CAPTURE=OFF).
NOT_COMPILED = ("Miniaudio.cpp", "MiniaudioImpl.cpp")


class Stale(Exception):
    """The C++ is not shaped the way this script reads it: fail, never guess."""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def from_cpp(source, symbol):
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def unknown_pieces(cpp):
    """Raw-string pieces in Shaders.cpp that this script does not copy."""
    declared = re.findall(r'const char\* const (k\w+) = R"\(', cpp)
    return [symbol for symbol in declared if symbol not in JS_NAME]


def pieces_of(expression, where):
    """`std::string( kVersion ) + kA + kB` -> [kVersion, kA, kB]."""
    pieces = []
    for term in (t.strip() for t in expression.split("+")):
        m = re.fullmatch(r"(?:std::string\(\s*)?(?:shaders::)?(k\w+)(?:\s*\))?", term)
        if m is None:
            raise Stale(f"{where} joins `{term}`, which is not a piece")
        pieces.append(m.group(1))
    return pieces


def assembly(cpp, plugin_cpp):
    """[(stage name, [JS pieces])] from Shaders.cpp's AssembleVertex() and
    AssembleFragment(), each one `return <pieces>;`, checked against
    Woofer.cpp's InitGL, which compiles exactly those two as its one program."""
    for needed in ("const std::string vertex   = shaders::AssembleVertex();",
                   "const std::string fragment = shaders::AssembleFragment();",
                   "shader.Compile( vertex.c_str(), fragment.c_str() )"):
        if needed not in plugin_cpp:
            raise Stale(f"Woofer.cpp's InitGL no longer has `{needed}`")
    if len(re.findall(r"\.Compile\(", plugin_cpp)) != 1:
        raise Stale("Woofer.cpp compiles more than one program; this script reads one")
    out = []
    for stage, function in (("vertex", "AssembleVertex"), ("fragment", "AssembleFragment")):
        body = re.search(r"std::string " + function + r"\(\)\s*\{(.*?)\n\}", cpp, re.S)
        if body is None:
            raise Stale(f"no std::string {function}() in source/Shaders.cpp")
        returns = re.findall(r"return (.*?);", body.group(1), re.S)
        if len(returns) != 1 or body.group(1).strip() != f"return {returns[0]};":
            raise Stale(f"{function}() is not one `return <pieces>;`")
        symbols = pieces_of(returns[0], f"{function}()")
        for symbol in symbols:
            if symbol not in JS_NAME:
                raise Stale(f"{function}() uses {symbol}, a piece this script does not copy")
        out.append((f"woofer_{stage}", [JS_NAME[s] for s in symbols]))
    return out


def render(cpp, plugin_cpp):
    """shaders.js as --write produces it."""
    version = cpp_version(cpp)
    if version is None:
        raise Stale("kVersion not found in source/Shaders.cpp")
    out = [HEADER]
    out.append(f"export const VERSION = '{version}';\n")
    for name, symbol in SHADERS:
        body = from_cpp(cpp, symbol)
        if body is None:
            raise Stale(f"{symbol} not found in source/Shaders.cpp")
        if "\\" in body or "${" in body:
            raise Stale(f"{symbol} holds a backslash or ${{; the escape scheme cannot carry it")
        out.append(f"\n// {symbol}, source/Shaders.cpp\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    out.append(
        "\n// Each stage's source, piece by piece, in the order the plugin joins them:\n"
        "// Shaders.cpp's AssembleVertex() and AssembleFragment(), the one program\n"
        "// Woofer.cpp's InitGL compiles.\n"
        "export const ASSEMBLY = {\n"
    )
    for name, pieces in assembly(cpp, plugin_cpp):
        out.append(f"  {name}: [{', '.join(repr(p) for p in pieces)}],\n")
    out.append("};\n")
    return "".join(out)


def js_version(js):
    match = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    return None if match is None else match.group(1)


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check_shaders(cpp, plugin_cpp, js):
    problems = 0

    for symbol in unknown_pieces(cpp):
        print(f"FAIL  {symbol} is a piece of source/Shaders.cpp this script does not copy -- add it to SHADERS")
        problems += 1

    version_cpp = cpp_version(cpp)
    if version_cpp is None or js_version(js) != version_cpp:
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<24} matches kVersion")

    for name, symbol in SHADERS:
        cpp_text = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            print(f"ok    {name:<24} matches {symbol} ({len(cpp_text)} chars)")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    try:
        table = assembly(cpp, plugin_cpp)
    except Stale as stale:
        print(f"FAIL  the plugin's assembly could not be read: {stale}")
        return problems + 1
    block = re.search(r"^export const ASSEMBLY = \{\n(.*?)^\};$", js, re.S | re.M)
    rows = {} if block is None else dict(re.findall(r"^  (\w+): \[(.*)\],$", block.group(1), re.M))
    for name, pieces in table:
        want = ", ".join(repr(p) for p in pieces)
        if rows.get(name) == want:
            print(f"ok    {'ASSEMBLY.' + name:<24} {' + '.join(pieces)}")
        else:
            print(f"FAIL  ASSEMBLY.{name} is [{rows.get(name, '<missing>')}], the plugin's is [{want}]")
            problems += 1
    for name in sorted(set(rows) - set(n for n, _ in table)):
        print(f"FAIL  ASSEMBLY.{name} is no stage of the plugin's")
        problems += 1

    try:
        expected = render(cpp, plugin_cpp)
    except Stale as stale:
        print(f"FAIL  {stale}")
        return problems + 1
    if expected != js:
        where = first_difference(expected, js)
        print("FAIL  demo/shaders.js is not exactly what --write produces")
        if where:
            print(f"        first difference at line {where[0]}")
            print(f"          want: {where[1]}")
            print(f"          have: {where[2]}")
        problems += 1
    return problems


#---------------------------------------------------------------------------
# The uniforms.
#---------------------------------------------------------------------------

def declared_uniforms(text):
    """{name: type} of every `uniform <type> <Name>;` in a stage's source."""
    out = {}
    for kind, name in re.findall(r"^\s*uniform\s+(\w+)\s+(\w+)\s*;", text, re.M):
        out[name] = kind
    return out


def check_uniforms(cpp, plugin_cpp):
    problems = 0
    try:
        table = assembly(cpp, plugin_cpp)
    except Stale:
        return 0  # already reported
    pieces = {name: from_cpp(cpp, symbol) or "" for name, symbol in SHADERS}
    pieces["VERSION"] = cpp_version(cpp) or ""
    text = "".join(pieces[p] for _, stage in table for p in stage)
    declared = declared_uniforms(text)
    calls = re.findall(r"(glUniform\w+)\(\s*where\(\s*\"(\w+)\"\s*\)", plugin_cpp)
    named = len(re.findall(r"\bwhere\(\s*\"", plugin_cpp))
    if named != len(calls):
        print(f"FAIL  Woofer.cpp names {named} uniforms through where() and this script reads {len(calls)} of them as glUniform calls")
        return 1
    set_names = [n for _, n in calls]
    for name in sorted(set(declared) - set(set_names)):
        print(f"FAIL  uniform {declared[name]} {name} is never set (a sampler would sit on unit 0)")
        problems += 1
    for setter, name in calls:
        if name not in declared:
            print(f"FAIL  {setter} sets {name}, which the program does not declare")
            problems += 1
        elif setter not in SETTER.get(declared[name], set()):
            print(f"FAIL  {name} is {declared[name]} and is set with {setter}, not {' or '.join(sorted(SETTER.get(declared[name], {'?'})))}")
            problems += 1
    duplicates = sorted({n for n in set_names if set_names.count(n) > 1})
    for name in duplicates:
        print(f"FAIL  {name} is set more than once a frame")
        problems += 1
    if problems == 0:
        print(f"ok    {'UNIFORMS':<24} all {len(declared)} set, each by its type's setter ({len(calls)} calls)")
    return problems


#---------------------------------------------------------------------------
# wftest's track, and no device.
#---------------------------------------------------------------------------

def function_text(source, name):
    """A top-level C++ function: from the line that declares `name(` at the
    start of a line (its return type first) to the first `}` at the start of
    a line after it."""
    match = re.search(r"^[A-Za-z_][\w:<>&* ]*\b" + name + r"\(.*?^\}$", source, re.S | re.M)
    return None if match is None else match.group(0)


def check_harness(main_cpp, glue):
    problems = 0
    for name in HARNESS:
        theirs = function_text(main_cpp, name)
        ours = function_text(glue, name)
        if theirs is None:
            print(f"FAIL  {name} not found in tools/wftest/main.cpp")
            problems += 1
        elif ours is None:
            print(f"FAIL  {name} not found in demo/wasm/glue.cpp")
            problems += 1
        elif ours == theirs:
            print(f"ok    {name:<24} matches tools/wftest/main.cpp's ({len(ours)} chars)")
        else:
            print(f"FAIL  {name} in demo/wasm/glue.cpp has drifted from tools/wftest/main.cpp's")
            where = first_difference(theirs, ours)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          wftest: {where[1]}")
                print(f"          glue  : {where[2]}")
            problems += 1
    wftest_lines = {line.strip() for line in main_cpp.splitlines()}
    # The glue's instance is wftest's rig: the one renaming the check allows.
    glue_lines = {line.strip().replace("instance->plugin.", "rig.plugin.") for line in glue.splitlines()}
    for line in HARNESS_LINES:
        missing = [where for where, lines in (("tools/wftest/", wftest_lines), ("demo/wasm/glue.cpp", glue_lines)) if line not in lines]
        if missing:
            print(f"FAIL  `{line}` is not in {' or '.join(missing)}")
            problems += 1
    if problems == 0:
        print(f"ok    {'HARNESS LINES':<24} all {len(HARNESS_LINES)} in both (kFeedRate, the types, the --host-fft spectrum)")
    return problems


def check_no_devices():
    problems = 0
    demo = os.path.join(REPO, "demo")
    served = []
    for root, dirs, files in os.walk(demo):
        rel = os.path.relpath(root, demo)
        if rel.split(os.sep)[0] in ("tools", "wasm"):
            continue  # not served (.assetsignore), and this file names them
        for name in files:
            if name.endswith((".js", ".html")):
                served.append(os.path.join(root, name))
    for path in sorted(served):
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        for api in DEVICE_APIS:
            if api in text:
                print(f"FAIL  {os.path.relpath(path, REPO)} names {api}: the page must never ask for or list an audio device")
                problems += 1
    if problems == 0:
        print(f"ok    {'NO DEVICES':<24} none of the {len(served)} served scripts and pages names {', '.join(DEVICE_APIS)}")
    return problems


#---------------------------------------------------------------------------
# The WebAssembly.
#---------------------------------------------------------------------------

def sha256(path):
    digest = hashlib.sha256()
    with open(os.path.join(REPO, path), "rb") as handle:
        for block in iter(lambda: handle.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def check_wasm():
    try:
        lines = read(MANIFEST).splitlines()
    except FileNotFoundError:
        print(f"FAIL  {MANIFEST} is missing -- run demo/tools/build-wasm.sh")
        return 1

    problems = 0
    entries = 0
    recorded = set()
    for line in lines:
        pin = re.match(r"^# FFGL SDK ([0-9a-f]{40})$", line)
        if pin:
            try:
                head = subprocess.run(["git", "-C", os.path.join(REPO, "external", "ffgl"), "rev-parse", "HEAD"],
                                      capture_output=True, text=True, check=True).stdout.strip()
            except (OSError, subprocess.CalledProcessError):
                print("FAIL  cannot read the FFGL SDK's commit -- is the submodule checked out?")
                problems += 1
                continue
            if head != pin.group(1):
                print(f"FAIL  the .wasm was built against FFGL SDK {pin.group(1)[:7]}, the submodule is at {head[:7]}")
                problems += 1
            continue
        if not line or line.startswith("#"):
            continue
        want, path = line.split(None, 1)
        recorded.add(path)
        entries += 1
        if not os.path.exists(os.path.join(REPO, path)):
            print(f"FAIL  {path} went into the .wasm and no longer exists")
            problems += 1
        elif sha256(path) != want:
            print(f"FAIL  {path} has changed since demo/woofer-core.wasm was built -- rerun demo/tools/build-wasm.sh")
            problems += 1

    # A source file added since the build is an input the build never saw.
    trees = [("source", REGISTRATION), (os.path.join("external", "earshot", "src"), NOT_COMPILED),
             (os.path.join("external", "earshot", "include", "earshot"), ())]
    for tree, skipped in trees:
        for name in sorted(os.listdir(os.path.join(REPO, tree))):
            if name in skipped:
                continue
            rel = f"{tree}/{name}".replace(os.sep, "/")
            if (name.endswith(".h") or name.endswith(".cpp")) and rel not in recorded:
                print(f"FAIL  {rel} is newer than the .wasm's manifest -- rebuild")
                problems += 1

    for output in OUTPUTS:
        if output not in recorded:
            print(f"FAIL  {MANIFEST} does not record {output}")
            problems += 1
    if problems == 0:
        print(f"ok    {'WASM':<24} built from these sources ({entries} files hashed, SDK pin agrees)")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    plugin_cpp = read("source", "Woofer.cpp")
    if "--write" in argv:
        unknown = unknown_pieces(cpp)
        if unknown:
            print(f"FAIL  {', '.join(unknown)}: piece(s) this script does not copy -- add them to SHADERS")
            return 1
        try:
            text = render(cpp, plugin_cpp)
        except Stale as stale:
            print(f"FAIL  {stale}")
            return 1
        with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
            handle.write(text)
        print("wrote demo/shaders.js")
        return 0
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check_shaders(cpp, plugin_cpp, js)
    problems += check_uniforms(cpp, plugin_cpp)
    wftest = "".join(read("tools", "wftest", name) for name in ("main.cpp", "checks.inc", "run.inc"))
    problems += check_harness(wftest, read("demo", "wasm", "glue.cpp"))
    problems += check_no_devices()
    problems += check_wasm()
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for the GLSL and"
              " demo/tools/build-wasm.sh for the .wasm; never edit either output by hand")
        return 1
    print(f"all {len(SHADERS) + 1} shader pieces and the order both stages join them in are the plugin's,"
          f" every uniform is set by its type's setter, the glue's {len(HARNESS)} track functions are wftest's,"
          " no served file names a device API, and the .wasm is built from the current sources")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
