#pragma once

#include <string>

/**
    One pass, straight into the host's framebuffer.

    The CPU has already moved the driver, projected it and rasterised it into
    the profile table (Profile.h): for every exposure instant, a row of bins
    in screen radius holding where that bin's material was printed from, its
    normal, its paint, its bare reflectance, its gloss and its axial
    displacement. The fragment shader finds its pixel's screen radius, reads
    each row there (texelFetch and its own linear blend, so every GPU reads
    the same numbers), fetches the clip from where the paint came from, lights
    it, and averages the rows: the exposure. Outside the driver it is the
    baffle, flat and matte, which returns the clip.

    No time and no trig reach the shader: the CPU sends directions.
*/
namespace woofer::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kWooferFragment;

/// The vertex and fragment sources as compiled (tools/glslc.sh mirrors this).
std::string AssembleVertex();
std::string AssembleFragment();

/// Hook bits for the negative controls.
constexpr int kHookAlphaPassthrough = 1;///< output alpha = src.a, whatever Mix

} // namespace woofer::shaders
