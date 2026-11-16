#!/usr/bin/env bash
#
# Everything, in the order that fails fastest. (ferrofluid's script by way of
# fountain; woofer's steps.)
#
# The build is universal and fresh on purpose. An arm64-only bundle builds and
# tests perfectly well here and then fails to load in an Intel Resolume, and
# the build log calls it a success either way -- so the architecture is
# checked with lipo, never with the log. KEEP_BUILD=1 reuses the build
# directory instead of deleting it first.
#
# No step opens, lists or asks for an audio device. wftest installs a scripted
# earshot backend before anything else runs, and oxbow -- which loads the real
# bundle, with the real backend -- runs with WOOFER_NO_DEVICES=1, which keeps
# the plugin's input on "None (host FFT)" and its device list empty.
#
# oxbow is required, not optional: a host's reading of the bundle is the one
# check nothing else here makes. Point OXBOW at a built oxbow.
#
#     OXBOW=/path/to/oxbow tools/verify.sh [BUILD_DIR]
#
# Run it under bash (it is a bash script): zsh has no PIPESTATUS, and the
# closed-stdout check below reads it.
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$REPO/build-verify}"

cd "$REPO"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
fail() { printf '\033[31mFAIL\033[0m %s\n' "$1"; exit 1; }

#---------------------------------------------------------------------------
# GLSL reserved words, declared as identifiers.
#
# glslc catches these, but glslc is optional -- so the trap most likely to be
# walked into gets its own grep that always runs. The list is GLSL 4.10's
# keywords reserved for future use (3.6) plus the ones already bitten:
# `packed` passed Apple's compiler and glslc and failed only Mesa (atrac);
# noise1..noise4 are built-ins a user function is silently shadowed by (dice).
#---------------------------------------------------------------------------
reserved_words() {
	local words="common partition active asm class union enum typedef template this resource goto inline noinline
	             public static extern external interface long short half fixed unsigned superp input output filter
	             sizeof cast namespace using packed sample patch layout flat smooth noperspective precise
	             noise1 noise2 noise3 noise4"
	local bad=0 word
	for word in $words; do
		if grep -nE "(float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|mat[234])[[:space:]]+$word[[:space:]]*[;=,)(\[]" \
		            source/Shaders.cpp >/dev/null 2>&1; then
			printf '   "%s" is declared as an identifier and is a GLSL reserved word\n' "$word"
			bad=$(( bad + 1 ))
		fi
	done
	[ "$bad" -eq 0 ] && printf '   none of the reserved words is used as an identifier\n'
	return "$bad"
}

step "GLSL reserved words"
reserved_words || fail "a GLSL reserved word is used as an identifier"

#---------------------------------------------------------------------------
step "Shaders"
#---------------------------------------------------------------------------
tools/glslc.sh || fail "a shader does not compile"

#---------------------------------------------------------------------------
step "Submodule and snapshot"
#---------------------------------------------------------------------------
if [[ ! -f external/ffgl/CMakeLists.txt ]]; then
	fail "FFGL SDK missing -- run: git submodule update --init --recursive"
fi
pin="$(git -C external/ffgl rev-parse --short=7 HEAD)"
[[ "$pin" == "b1afaf9" ]] || fail "FFGL SDK at $pin, not the fleet's b1afaf9"
echo "ok   FFGL SDK pinned at $pin"
[[ -f external/earshot/SNAPSHOT ]] || fail "external/earshot/SNAPSHOT missing: which earshot is this?"
snap="$( sed -n 's/^Commit:[[:space:]]*\([0-9a-f]*\).*/\1/p' external/earshot/SNAPSHOT | head -1 )"
[[ -n "$snap" ]] || fail "external/earshot/SNAPSHOT names no commit"
patches="$( sed -n 's/^Patches:[[:space:]]*//p' external/earshot/SNAPSHOT | head -1 )"
[[ -n "$patches" ]] || fail "external/earshot/SNAPSHOT does not say what was patched ('Patches:  none.' if nothing)"
echo "ok   earshot snapshot of ${snap:0:7}, patches: $patches"

#---------------------------------------------------------------------------
step "Build (universal, Release, fresh)"
#---------------------------------------------------------------------------
[[ "${KEEP_BUILD:-0}" == 1 ]] || rm -rf "$BUILD"
log="$( mktemp )"
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >"$log" 2>&1 || { cat "$log"; fail "configure"; }
cmake --build "$BUILD" -j8 >"$log" 2>&1 || { tail -40 "$log"; fail "build"; }
# The SDK's own FFGLUtilities.cpp warns about RAND_MAX, and earshot is a
# snapshot this repo does not edit; anything else is ours.
if grep -E 'warning:' "$log" | grep -v -e 'external/ffgl' -e 'external/earshot' | grep -q .; then
	grep -E 'warning:' "$log" | grep -v -e 'external/ffgl' -e 'external/earshot' | sed 's/^/   /'
	fail "a warning in this repo's code"
fi
rm -f "$log"
echo "ok   built, no warnings outside the SDK and the earshot snapshot"
WFTEST="$BUILD/wftest"

#---------------------------------------------------------------------------
step "Bundle"
#---------------------------------------------------------------------------
OXBOW="${OXBOW:-$HOME/Projects/resolume/oxbow/build/oxbow}"
[[ -x "$OXBOW" ]] || fail "no oxbow at $OXBOW -- set OXBOW=/path/to/oxbow; a host's reading is not optional here"
declared="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1 )"
grep -q "versionFallback = \"v$declared\"" source/StoatworksAbout.h || fail "StoatworksAbout.h's versionFallback is not v$declared"
check_bundle() {
	local name="$1" id="$2" display="$3" ffid="$4" kind="$5"
	local bundle="$BUILD/$name.bundle" binary="$BUILD/$name.bundle/Contents/MacOS/$name"
	[[ -f "$binary" ]] || fail "no binary at $binary"
	local arches; arches="$(lipo -archs "$binary")"
	[[ "$arches" == *arm64* ]]  || fail "$name: no arm64 slice (got: $arches)"
	[[ "$arches" == *x86_64* ]] || fail "$name: no x86_64 slice (got: $arches)"
	# Captured, then matched from a herestring -- never `nm ... | grep -q`: under
	# pipefail the grep's early exit SIGPIPEs nm and fails the pipeline.
	local symbols; symbols=$( nm -gU "$binary" 2>/dev/null || true )
	grep -q '_plugMain' <<<"$symbols" || fail "$name: plugMain not exported"
	local plist="$bundle/Contents/Info.plist"
	[[ -f "$plist" ]] || fail "$name: no Info.plist"
	read_plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null || true; }
	[[ "$( read_plist CFBundleIdentifier )" == "$id" ]] || fail "$name: bundle id is '$( read_plist CFBundleIdentifier )'"
	[[ "$( read_plist CFBundleExecutable )" == "$name" ]] || fail "$name: CFBundleExecutable is wrong"
	[[ "$( read_plist CFBundlePackageType )" == "BNDL" ]] || fail "$name: CFBundlePackageType is not BNDL"
	[[ "$( read_plist CFBundleVersion )" == "$declared" ]] || fail "$name: plist version != CMakeLists $declared"
	codesign --force --sign - --timestamp=none "$bundle" >/dev/null 2>&1 || fail "$name: could not be ad-hoc signed"
	codesign --verify --deep --strict "$bundle" >/dev/null 2>&1 || fail "$name: the ad-hoc signature does not verify"
	echo "ok   $name: $arches, plugMain exported, $id, BNDL, v$declared, ad-hoc signed and verified"
	# What a host reads: id, name, type. The FFGL name field is char[16] and not
	# null-terminated, so only something reading it back as a host does notices
	# a truncation. WOOFER_NO_DEVICES=1: oxbow loads the real backend, and the
	# plugin must neither list nor open an input under it.
	local probe; probe="$( WOOFER_NO_DEVICES=1 "$OXBOW" probe "$bundle" 2>&1 || true )"
	printf '%s\n' "$probe" | sed -n '1,3p' | sed 's/^/   /'
	grep -q "id:          $ffid" <<<"$probe" || fail "oxbow did not read the id $ffid"
	grep -q "name:        $display\$" <<<"$probe" || fail "oxbow did not read the name '$display'"
	grep -q "type:        $kind" <<<"$probe" || fail "oxbow did not read the type as $kind"
	local selftest; selftest="$( WOOFER_NO_DEVICES=1 "$OXBOW" selftest "$bundle" 2>&1 || true )"
	grep -q "selftest:    PASS" <<<"$selftest" || { printf '%s\n' "$selftest" | tail -5; fail "oxbow selftest failed on $name"; }
	echo "ok   a host reads $display $ffid $kind, and oxbow selftest renders through it"
}
check_bundle "Woofer" "com.stoatworks.ffgl.woofer" "SW Woofer" "WF01" "effect"

#---------------------------------------------------------------------------
step "Checks (this Mac's GPU, 1280x720 and 320x180)"
#---------------------------------------------------------------------------
# Every claim the README makes, one flag each, each at both rasters. The list
# is read from wftest itself, so a new check cannot be left out here.
CHECKS="$( "$WFTEST" --help | sed -n 's/^  checks, one flag each://p' | tr -d '-' )"
[[ -n "$CHECKS" ]] || fail "could not read the list of checks from wftest --help"
for check in $CHECKS; do
	if out=$( "$WFTEST" --$check 2>&1 ); then
		printf '   ok   wftest --%s: %s measurements\n' "$check" "$( printf '%s\n' "$out" | grep -c '^  ok' )"
	else
		printf '%s\n' "$out" | { grep -E 'FAIL|^wftest:' || printf '%s\n' "$out" | tail -5; } | sed 's/^/      /'
		fail "wftest --$check"
	fi
done

#---------------------------------------------------------------------------
step "Software renderer (CI's, at 320x180)"
#---------------------------------------------------------------------------
# The picture checks on Apple's software renderer, which is what a GPU-less CI
# runner falls back to: the rest pose, the pinhole, the alpha, the resize, the
# fallback's lamp and the device menu. The physics checks are CPU and the same
# on any renderer; they ran above and run again under --offline.
SOFTWARE="rest magnify alpha resize fallback devices"
for check in $SOFTWARE; do
	if out=$( WFTEST_RENDERER=software "$WFTEST" --$check --size 320x180 2>&1 ); then
		printf '   ok   wftest --%s (software): %s measurements\n' "$check" "$( printf '%s\n' "$out" | grep -c '^  ok' )"
	else
		printf '%s\n' "$out" | { grep -E 'FAIL|^wftest:' || printf '%s\n' "$out" | tail -5; } | sed 's/^/      /'
		fail "wftest --$check on the software renderer -- run: WFTEST_RENDERER=software $WFTEST --$check --size 320x180"
	fi
done

#---------------------------------------------------------------------------
step "Offline (what CI would run)"
#---------------------------------------------------------------------------
WFTEST_NO_GL=1 "$WFTEST" --offline >/dev/null || fail "wftest --offline"
echo "ok   wftest --offline, with no GL context"

#---------------------------------------------------------------------------
step "Pipe"
#---------------------------------------------------------------------------
# The fleet's --pipe frame format, which the videos render through. Two and a
# half frames in must be exactly two out and a clean exit -- a partial frame is
# the end of the stream, never a frame -- --film N must be N frames, a cue
# naming no parameter, a --set that is not a number or an option and a --set
# of nan must be refused, and a reader that hangs up must end the run with
# exit 1, not SIGPIPE's silent 141. The same with --wav: the audio comes from
# a file, fed through earshot's feed() at the clip's frame rate.
frame=$(( 64 * 36 * 4 ))
scratch="$( mktemp -d )"
raw="$scratch/in.rgba"; cues="$scratch/cues"; out="$scratch/out.rgba"; wav="$scratch/tone.wav"
python3 - "$wav" <<'PY'
import math, struct, sys, wave
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(48000)
    w.writeframes(b"".join(struct.pack("<hh", s, s) for s in
        (int(12000 * math.sin(2 * math.pi * 50 * n / 48000)) for n in range(48000))))
PY
for flavour in "" "--wav $wav"; do
	label=${flavour:+--wav}; label=${label:-track}
	head -c $(( frame * 5 / 2 )) /dev/zero > "$raw"
	status=0
	"$WFTEST" $flavour --pipe --size 64x36 < "$raw" > "$out" 2>/dev/null || status=$?
	got=$( wc -c < "$out" | tr -d ' ' )
	[[ "$status" -eq 0 && "$got" == "$(( frame * 2 ))" ]] \
		|| fail "$label: 2.5 frames in gave $got bytes out (want $(( frame * 2 ))), exit $status"
	status=0
	"$WFTEST" $flavour --film 3 --size 64x36 > "$out" 2>/dev/null || status=$?
	got=$( wc -c < "$out" | tr -d ' ' )
	[[ "$status" -eq 0 && "$got" == "$(( frame * 3 ))" ]] || fail "$label: --film 3 gave $got bytes, exit $status"
	# Read from a file, not a pipe: a writer killed by SIGPIPE would fail the
	# pipeline whatever wftest did, and the refusal would pass for the wrong reason.
	printf '0 No Such Control 0.5\n' > "$cues"
	status=0
	"$WFTEST" $flavour --pipe --size 64x36 --script "$cues" < "$raw" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 2 ]] || fail "$label: a cue naming no parameter gave exit $status, not 2"
	for bad in "Driver=Nope" "Drive=abc" "Drive=nan"; do
		status=0
		"$WFTEST" $flavour --film 1 --size 64x36 --set "$bad" >/dev/null 2>&1 || status=$?
		[[ "$status" -eq 2 ]] || fail "$label: --set '$bad' gave exit $status, not 2"
	done
	status=0
	"$WFTEST" $flavour --film 1 --size 64x36 --set "Driver=26 cm Woofer" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 0 ]] || fail "$label: --set 'Driver=26 cm Woofer' (an option by name) gave exit $status"
	head -c $(( frame * 20 )) /dev/zero > "$raw"
	set +e
	"$WFTEST" $flavour --pipe --size 64x36 < "$raw" 2>/dev/null | head -c 1 >/dev/null
	s1=${PIPESTATUS[0]}
	"$WFTEST" $flavour --film 20 --size 64x36 2>/dev/null | head -c 1 >/dev/null
	s2=${PIPESTATUS[0]}
	set -e
	[[ "$s1" -eq 1 && "$s2" -eq 1 ]] || fail "$label: a closed stdout gave exit $s1 (--pipe), $s2 (--film), not 1"
	echo "ok   $label: 2.5 frames in, exactly 2 out; --film 3 is 3; a bad cue, a bad option, abc and nan refused (exit 2); | head -c 1 exits 1, not 141"
done
# A file that is not a WAV must be refused, not read as silence.
printf 'not a wav' > "$scratch/bad.wav"
status=0
"$WFTEST" --wav "$scratch/bad.wav" --film 1 --size 64x36 >/dev/null 2>&1 || status=$?
[[ "$status" -eq 2 ]] || fail "--wav of a file that is not a WAV gave exit $status, not 2"
echo "ok   --wav of a file that is not a WAV refused (exit 2)"
rm -rf "$scratch"

#---------------------------------------------------------------------------
step "Negative controls"
#---------------------------------------------------------------------------
# Every check above, against a model that is deliberately wrong, required to
# FAIL. A check that cannot fail is not a check.
if out=$( "$WFTEST" --negative 2>&1 ); then
	printf '%s\n' "$out" | grep -E '^negative controls' | sed 's/^/   /'
else
	printf '%s\n' "$out" | { grep -E 'PASSED against|undetected|^wftest:' || printf '%s\n' "$out" | tail -5; } | sed 's/^/      /'
	fail "a negative control went undetected"
fi

#---------------------------------------------------------------------------
step "Mutants"
#---------------------------------------------------------------------------
tools/mutate.sh | grep -E '^== mutant|^   (ok|FAIL)|^mutants' || fail "a mutant survived"

#---------------------------------------------------------------------------
step "Dead controls"
#---------------------------------------------------------------------------
# The only thing that catches a uniform whose name does not match the C++.
python3 tools/sweep.py --build "$BUILD" || fail "a dead control"

#---------------------------------------------------------------------------
step "Cost"
#---------------------------------------------------------------------------
"$WFTEST" --bench

printf '\n\033[32mall green\033[0m\n'
