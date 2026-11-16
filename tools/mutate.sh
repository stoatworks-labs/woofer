#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders, or its own copy of the driver, would pass every mutant here.
# (ferrofluid's script by way of fountain; the mutants are woofer's.)
#
# The tree: one copy of the repo in a temporary directory (the FFGL SDK
# symlinked), configured arm64-only and built once, so earshot -- which no
# mutant touches -- is compiled once rather than sixteen times. Before EACH
# mutant, source/ and tools/ are copied afresh from the repo and EVERY object
# of woofer's own code is deleted, so the build recompiles all of it. That is
# the answer to macOS make's one-second mtimes: a restored file can be no
# newer than the object the previous mutant left, and make would happily link
# the previous mutant (the fleet's trap) -- with the objects gone, it cannot.
#
# Each mutant runs only its named check, at CI's raster (320x180), under a
# perl alarm (a mutant can hang: honeydew's lesson; a timeout counts as
# caught), with WFTEST_FIRST_FAILURE=1 so a long check stops at its first FAIL.
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap).
MUTANTS=(
	"source/Shaders.cpp|vec2 from   = centre + a.x * dir;|vec2 from   = centre - a.x * dir;|--rest|GLSL: the clip fetched mirrored through the centre, + -> -"
	"source/Shaders.cpp|float x = bins - 0.5;|float x = bins - 1.5;|--rest|GLSL: the table read a bin off, 0.5 -> 1.5"
	"source/Shaders.cpp|toLinear( clip.rgb ), clip.a * a.w );|toLinear( clip.rgb ), clip.a * a.z );|--rest|GLSL: the paint read from the normal's z, w -> z"
	"source/Shaders.cpp|float lit = 1.0 / ( Ambient|float lit = 2.0 / ( Ambient|--rest|GLSL: the diffuse normalisation doubled, 1 -> 2"
	"source/Shaders.cpp|vec3( 0.5 + lift * HeightScale )|vec3( 0.6 + lift * HeightScale )|--pumping|GLSL: the Height view's rest at 0.6, 5 -> 6"
	"source/Shaders.cpp|fragColor = blend( src, vec4( shown, 1.0 ), MixAmount );|fragColor = blend( src, vec4( shown, 0.0 ), MixAmount );|--alpha|GLSL: the driver transparent, 1 -> 0"
	"source/Driver.cpp|- Fsus( s.x ) + Fbox( s.x ) ) / mms;|- Fsus( s.x ) - Fbox( s.x ) ) / mms;|--smallsignal|C++: the air spring pushing the wrong way, + -> -"
	"source/Driver.cpp|std::exp( -kGamma * std::log1p|std::exp( +kGamma * std::log1p|--harmonics|C++: the adiabatic exponent's sign, - -> +"
	"source/Driver.cpp|return 0.5 * ( 2.0 * p1 + ( p2 - p0 ) * u|return 0.5 * ( 2.0 * p1 + ( p2 + p0 ) * u|--smallsignal|C++: Catmull-Rom's tangent, - -> +"
	"source/Driver.cpp|( s.qtc / d.Qts ) * ( s.qtc / d.Qts ) - 1.0 : 0.0;|( s.qtc / d.Qts ) * ( s.qtc / d.Qts ) - 2.0 : 0.0;|--smallsignal|C++: alpha = (Qtc/Qts)^2 - 2, 1 -> 2"
	"source/Driver.cpp|const double c = x + ( perturb.ignoreOffset ? 0.0 : x0 );|const double c = x - ( perturb.ignoreOffset ? 0.0 : x0 );|--drift|C++: the coil's offset applied backwards, + -> -"
	"source/Profile.cpp|return kp * r / ( dc - z );|return kp * r / ( dc + z );|--magnify|C++: the pinhole's depth with the wrong sign, - -> +"
	"source/Profile.cpp|const double f = std::sin( t ) - k * t;|const double f = std::sin( t ) + k * t;|--surround|C++: the roll's arc solve, - -> +"
	"source/Audio.cpp|const double step  = 2.0 * kPi * hz / DriverModel::kTapRate;|const double step  = 3.0 * kPi * hz / DriverModel::kTapRate;|--fallback|C++: the fallback's sines 1.5x too high, 2 -> 3"
	"source/Woofer.cpp|clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;|clockScale  = hostDelta > 1.0 ? 1.0 : 0.001;|--fallback|C++: the clock-unit fallback upside down, < -> >"
	"source/Woofer.cpp|set( 1.0f, 0.62f, 0.05f );|set( 1.0f, 0.52f, 0.05f );|--fallback|C++: the amber lamp off-colour, 6 -> 5"
	"source/Audio.cpp|name = known != seen.end() ? known->second|name = known == seen.end() ? known->second|--devices|C++: a remembered key resolved backwards, ! -> ="
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
cp -R "$REPO/external/earshot" "$tree/external/earshot"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

fresh() {
	rm -rf "$tree/source" "$tree/tools"
	cp -R "$REPO/source" "$REPO/tools" "$tree/"
	# Every object of woofer's own code: none may survive into the next build.
	rm -rf "$tree/build/CMakeFiles/woofer_core.dir" "$tree/build/CMakeFiles/wftest.dir" "$tree/build/wftest"
}

fresh
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
cmake --build "$tree/build" --target wftest -j8 >/dev/null 2>&1

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	fresh

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	# Reconfigure: the tools/ and source/ directories were replaced.
	cmake -S "$tree" -B "$tree/build" >/dev/null
	if ! cmake --build "$tree/build" --target wftest -j8 >"$WORK/build.log" 2>&1; then
		printf '   FAIL  the mutant did not build -- a mutant must compile to mean anything\n'
		tail -5 "$WORK/build.log" | sed 's/^/        /'
		continue
	fi
	status=0
	WFTEST_FIRST_FAILURE=1 limit 300 "$tree/build/wftest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
