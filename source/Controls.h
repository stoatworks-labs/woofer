#pragma once

#include "StoatworksAboutLinks.h"

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse for the defaults and the harness. Option
    parameters hold the element index (an option reads back 0..1 whatever its
    count -- map by index), except Audio Input, whose element values are
    earshot's device keys so a composition remembers the device by name.

    The host index IS the ParamId: one plugin, one dense list, About last.
*/
namespace woofer
{

enum ParamId : unsigned int
{
	// -- Driver ------------------------------------------------------------------
	PT_DRIVER = 0,
	PT_BOX,
	PT_XMAX,
	PT_OFFSET,
	PT_BREAKUP,

	// -- Audio -------------------------------------------------------------------
	PT_INPUT,
	PT_RESCAN,
	PT_CHANNEL,
	PT_DRIVE,
	PT_FFT_LAW,
	PT_AUDIO,

	// -- Look --------------------------------------------------------------------
	PT_SIZE,
	PT_CAMERA,
	PT_EXPOSURE,
	PT_RIPPLE,
	PT_LIGHT_ANGLE,
	PT_LIGHT_TURN,
	PT_AMBIENT,
	PT_GLOSS,
	PT_LAMP,
	PT_VIEW,
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event,
	Buffer,
	Devices///< Audio Input: elements filled from the device list
};

struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_TEXT.
const ControlInfo& InfoOf( unsigned int id );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

enum class Channel
{
	Mean = 0,
	Left,
	Right,
	Count
};
enum class FftLaw
{
	LinearMagnitude = 0,
	LinearPower,
	LogMagnitude,
	LogPower,
	Count
};
enum class View
{
	Lit = 0,
	Height,
	Count
};

/// Audio Input's fixed slots: element 0 is the host FFT, 1 the system
/// default, then devices, then "(none)" spares, kInputSlots in all.
constexpr int kInputSlots = 16;
constexpr const char* kHostFftLabel = "None (host FFT)";
constexpr const char* kDefaultLabel = "System default";
constexpr const char* kEmptySlot    = "(none)";

//---------------------------------------------------------------------------
// Conversions, 0..1 to physical and back.
//---------------------------------------------------------------------------
double QtcFromParam( float v );///< 0.3..1.5, linear
float ParamFromQtc( double qtc );
double XmaxFromParam( float v );///< x0.25..x2, geometric
float ParamFromXmax( double scale );
double OffsetFromParam( float v );///< -0.3..0.3 x gap height
float ParamFromOffset( double fraction );
double BreakupFromParam( float v );///< x0.25..x4, geometric
float ParamFromBreakup( double scale );
double DriveFromParam( float v );///< dB re V_ref, -20..+30
float ParamFromDrive( double db );
double SizeFromParam( float v );///< x frame height, 0.3..1.5
float ParamFromSize( double size );
double CameraFromParam( float v );///< x overall diameter, 1..5, geometric
float ParamFromCamera( double camera );
double ExposureFromParam( float v );///< seconds, 0..0.040
float ParamFromExposure( double seconds );
double RippleFromParam( float v );///< display gain, 0..1000, quadratic
float ParamFromRipple( double gain );
double LightAngleFromParam( float v );///< elevation, radians, 10..90 degrees
float ParamFromLightAngle( double degrees );
double LightTurnFromParam( float v );///< azimuth, radians, 0..360 degrees
float ParamFromLightTurn( double degrees );

} // namespace woofer
