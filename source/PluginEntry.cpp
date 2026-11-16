#include "Woofer.h"

/**
    The registration. Listed in the MODULE target, not the core: in the OBJECT
    library it would be linked into the harness too, and an FFGL binary
    resolves exactly one plugMain.
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< woofer::WooferPlugin >,// Create method
	"WF01",                              // Plugin unique ID of maximum length 4
	"SW Woofer",                         // Plugin name
	2,                                   // API major version number
	1,                                   // API minor version number
	0,                                   // Plugin major version number
	1,                                   // Plugin minor version number
	FF_EFFECT,                           // Plugin type
	"The clip printed on a loudspeaker cone that the music drives: a real driver from a real datasheet, in a sealed "
	"box, integrated at the audio rate. Kicks pump it, hi-hats leave it still, big hits flat-top its stroke and the coil "
	"bottoms at Xmech.",
	"Woofer FFGL effect"// About
);

extern "C" const char* WooferBuildStamp()
{
	return "woofer " WOOFER_VERSION ", built " __DATE__ " " __TIME__;
}
