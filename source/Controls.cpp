#include "Controls.h"

#include "Driver.h"

#include <algorithm>
#include <cmath>

namespace woofer
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

constexpr double kQtcLow = 0.30, kQtcHigh = 1.50;
constexpr double kXmaxLow = 0.25, kXmaxHigh = 2.0;
constexpr double kOffsetHigh = 0.30;
constexpr double kBreakupLow = 0.25, kBreakupHigh = 4.0;
constexpr double kDriveLow = -20.0, kDriveHigh = 30.0;
constexpr double kSizeLow = 0.30, kSizeHigh = 1.50;
constexpr double kCameraLow = 1.0, kCameraHigh = 5.0;
constexpr double kExposureHigh = 0.040;
constexpr double kRippleHigh = 1000.0;
constexpr double kAngleLow = 10.0, kAngleHigh = 90.0;

const char* const kDriverNames[ kPresetCount ] = { "10 cm Full-range", "18 cm Midwoofer", "26 cm Woofer", "30 cm Subwoofer",
	                                               "18 in PA Sub" };
const char* const kChannelNames[] = { "L+R", "Left", "Right" };
const char* const kLawNames[]     = { "Linear Mag", "Linear Power", "Log Mag", "Log Power" };
const char* const kLampNames[]    = { "Off", "On" };
const char* const kViewNames[]    = { "Lit", "Height" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}

//---------------------------------------------------------------------------
// The defaults: the 30 cm sub in a Qtc 0.707 box, driven 18 dB over the
// voltage that holds it at Xlin -- Resolume's demo stems sit at -20 to -32
// dBFS RMS, and at +18 dB a stem's kick moves the cone 17 % of Xmech while a
// -12 dBFS master reaches 40 % without the stop -- with the coil set a tenth
// of the gap out so the drift is there to see; a 1.5x-diameter camera; an
// 8 ms exposure; a low ambient and a glossy roll, so a black or dark clip
// still reads as a loudspeaker. AGENTS.md ("Defaults") says how they were
// checked over Resolume's clips.
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	option( PT_DRIVER, "Driver", "Driver", static_cast< float >( kDefaultPreset ), kDriverNames, kPresetCount ),
	standard( PT_BOX, "Box Qtc", "Driver", ParamFromQtc( 0.707 ) ),
	standard( PT_XMAX, "Xmax", "Driver", ParamFromXmax( 1.0 ) ),
	standard( PT_OFFSET, "Coil Offset", "Driver", ParamFromOffset( 0.1 ) ),
	standard( PT_BREAKUP, "Breakup", "Driver", ParamFromBreakup( 1.0 ) ),

	{ PT_INPUT, "Audio Input", "Audio", Kind::Devices, 0.0f, nullptr, 0 },
	{ PT_RESCAN, "Rescan Inputs", "Audio", Kind::Event, 0.0f, nullptr, 0 },
	option( PT_CHANNEL, "Channel", "Audio", 0.0f, kChannelNames, static_cast< int >( Channel::Count ) ),
	standard( PT_DRIVE, "Drive", "Audio", ParamFromDrive( 18.0 ) ),
	option( PT_FFT_LAW, "FFT Law", "Audio", 0.0f, kLawNames, static_cast< int >( FftLaw::Count ) ),
	{ PT_AUDIO, "Audio", "Audio", Kind::Buffer, 0.0f, nullptr, 0 },

	standard( PT_SIZE, "Size", "Look", ParamFromSize( 0.95 ) ),
	standard( PT_CAMERA, "Camera", "Look", ParamFromCamera( 1.5 ) ),
	standard( PT_EXPOSURE, "Exposure", "Look", ParamFromExposure( 0.008 ) ),
	standard( PT_RIPPLE, "Ripple", "Look", ParamFromRipple( 150.0 ) ),
	standard( PT_LIGHT_ANGLE, "Light Angle", "Look", ParamFromLightAngle( 40.0 ) ),
	standard( PT_LIGHT_TURN, "Light Turn", "Look", ParamFromLightTurn( 135.0 ) ),
	standard( PT_AMBIENT, "Ambient", "Look", 0.2f ),
	standard( PT_GLOSS, "Gloss", "Look", 0.85f ),
	option( PT_LAMP, "Status Lamp", "Look", 1.0f, kLampNames, 2 ),
	option( PT_VIEW, "View", "Look", 0.0f, kViewNames, static_cast< int >( View::Count ) ),
	standard( PT_MIX, "Mix", "Look", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "one row per control, in ParamId order" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

int OptionIndex( float value, int count )
{
	if( !std::isfinite( value ) )
		return 0;
	return static_cast< int >( std::clamp< long >( std::lround( value ), 0L, static_cast< long >( count - 1 ) ) );
}

double QtcFromParam( float v ) { return linear( v, kQtcLow, kQtcHigh ); }
float ParamFromQtc( double qtc ) { return inverseLinear( qtc, kQtcLow, kQtcHigh ); }
double XmaxFromParam( float v ) { return geometric( v, kXmaxLow, kXmaxHigh ); }
float ParamFromXmax( double scale ) { return inverseGeometric( scale, kXmaxLow, kXmaxHigh ); }
double OffsetFromParam( float v ) { return linear( v, -kOffsetHigh, kOffsetHigh ); }
float ParamFromOffset( double fraction ) { return inverseLinear( fraction, -kOffsetHigh, kOffsetHigh ); }
double BreakupFromParam( float v ) { return geometric( v, kBreakupLow, kBreakupHigh ); }
float ParamFromBreakup( double scale ) { return inverseGeometric( scale, kBreakupLow, kBreakupHigh ); }
double DriveFromParam( float v ) { return linear( v, kDriveLow, kDriveHigh ); }
float ParamFromDrive( double db ) { return inverseLinear( db, kDriveLow, kDriveHigh ); }
double SizeFromParam( float v ) { return linear( v, kSizeLow, kSizeHigh ); }
float ParamFromSize( double size ) { return inverseLinear( size, kSizeLow, kSizeHigh ); }
double CameraFromParam( float v ) { return geometric( v, kCameraLow, kCameraHigh ); }
float ParamFromCamera( double camera ) { return inverseGeometric( camera, kCameraLow, kCameraHigh ); }
double ExposureFromParam( float v ) { return linear( v, 0.0, kExposureHigh ); }
float ParamFromExposure( double seconds ) { return inverseLinear( seconds, 0.0, kExposureHigh ); }
double RippleFromParam( float v ) { return kRippleHigh * clamp01( v ) * clamp01( v ); }
float ParamFromRipple( double gain ) { return static_cast< float >( std::sqrt( std::clamp( gain / kRippleHigh, 0.0, 1.0 ) ) ); }
double LightAngleFromParam( float v ) { return linear( v, kAngleLow, kAngleHigh ) * kPi / 180.0; }
float ParamFromLightAngle( double degrees ) { return inverseLinear( degrees, kAngleLow, kAngleHigh ); }
double LightTurnFromParam( float v ) { return linear( v, 0.0, 360.0 ) * kPi / 180.0; }
float ParamFromLightTurn( double degrees ) { return inverseLinear( degrees, 0.0, 360.0 ); }

} // namespace woofer
