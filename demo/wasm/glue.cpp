/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call. (mirrorball's glue, which is
    fountain's, with woofer's names and earshot's feed.)

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Woofer.cpp` and everything it calls -- the driver and its
    integrator, the profile table, the audio input and earshot under it --
    compiled UNMODIFIED into the same WebAssembly module as this file (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs a `WooferPlugin`, as PluginEntry.cpp's registration
        would, and calls InitGL / DeInitGL on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTime, the Audio
        buffer's elements (Resolume's FFT) and ProcessOpenGL with one input
        texture, as `tools/wftest`'s Rig does.

    One thing here is the plugin's HARNESS rather than a host: the audio.
    wftest never opens a device; it hands the plugin its track through
    earshot's feed() (`WooferPlugin::FeedForTest`, which is
    `AudioInput::Feed`, which is `Engine::feedBegin` and `Engine::feed`), the
    path a device's samples take inside earshot. So does this page. `musicAt`
    and the five functions under it are wftest's own track, copied from
    tools/wftest/main.cpp character for character (and held to it by
    demo/tools/check_shaders.py), and `wf_host_fft` writes wftest
    --host-fft's synthetic kick spectrum into the Audio buffer (its formula
    line held to wftest's the same way). A file the visitor chooses is decoded
    by the page and reaches feed() through `wf_feed`, as `wftest --wav` does.

    No audio device exists in this module: earshot is built with
    EARSHOT_CAPTURE=OFF (NoCapture.cpp; miniaudio is not compiled), and
    `wf_prepare` sets WOOFER_NO_DEVICES=1, verify.sh's switch, so the
    plugin's AudioInput::Refresh never asks for a list. Nothing calls
    selectDevice.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the four entry points that cannot be passed
    straight through.
*/
#include "Controls.h"
#include "Diag.h"
#include "Driver.h"
#include "Woofer.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace woofer;

namespace
{
using Floats  = std::vector< float >;
using Doubles = std::vector< double >;

//---------------------------------------------------------------------------
// tools/wftest/main.cpp's track, verbatim (check_shaders.py compares).
//---------------------------------------------------------------------------
constexpr int kFeedRate = 48000;

/// A raised-cosine fade-in over `ramp` seconds, so a sine starts without the
/// transient that would throw the cone into its stop.
double fadeIn( double t, double ramp )
{
	return t >= ramp ? 1.0 : 0.5 - 0.5 * std::cos( kPi * t / ramp );
}

/// A kick: an exponential sweep 150 -> 50 Hz, a 150 ms decay.
double kickAt( double t )
{
	if( t < 0.0 || t > 0.6 )
		return 0.0;
	const double f0 = 150.0, f1 = 50.0, tau = 0.04;
	const double phase = 2.0 * kPi * ( f1 * t + ( f0 - f1 ) * tau * ( 1.0 - std::exp( -t / tau ) ) );
	return std::sin( phase ) * std::exp( -t / 0.15 );
}

/// The hi-hat: white noise with a 50 ms decay, THEN an 8th-order Butterworth
/// high-pass at 7 kHz (four RBJ biquads, Q from the Butterworth poles), so
/// the envelope's own onset cannot put energy below the corner. Generated
/// once at kFeedRate; peak-normalised to 1.
const Floats& hatSamples()
{
	static Floats hat;
	if( !hat.empty() )
		return hat;
	const int n = kFeedRate / 4;
	hat.resize( static_cast< size_t >( n ) );
	Doubles x( static_cast< size_t >( n ) );
	uint32_t state = 12345u;
	for( int i = 0; i < n; ++i )
	{
		state = state * 1664525u + 1013904223u;
		x[ static_cast< size_t >( i ) ] = ( ( state >> 8 ) * ( 1.0 / 16777216.0 ) * 2.0 - 1.0 ) * std::exp( -static_cast< double >( i ) / kFeedRate / 0.05 );
	}
	for( int s = 0; s < 4; ++s )
	{
		const double q  = 1.0 / ( 2.0 * std::sin( kPi * ( 2.0 * s + 1.0 ) / 16.0 ) );
		const double w0 = 2.0 * kPi * 7000.0 / kFeedRate;
		const double al = std::sin( w0 ) / ( 2.0 * q ), cw = std::cos( w0 );
		const double b0 = ( 1.0 + cw ) / 2.0, b1 = -( 1.0 + cw ), b2 = ( 1.0 + cw ) / 2.0;
		const double a0 = 1.0 + al, a1 = -2.0 * cw, a2 = 1.0 - al;
		double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
		for( int i = 0; i < n; ++i )
		{
			const double in = x[ static_cast< size_t >( i ) ];
			const double y  = ( b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2 ) / a0;
			x2 = x1;
			x1 = in;
			y2 = y1;
			y1 = y;
			x[ static_cast< size_t >( i ) ] = y;
		}
	}
	double peak = 0.0;
	for( int i = 0; i < n; ++i )
		peak = std::max( peak, std::fabs( x[ static_cast< size_t >( i ) ] ) );
	for( int i = 0; i < n; ++i )
		hat[ static_cast< size_t >( i ) ] = static_cast< float >( x[ static_cast< size_t >( i ) ] / peak );
	return hat;
}

double hatAt( double t )
{
	const Floats& hat = hatSamples();
	const long i      = std::lround( t * kFeedRate );
	return i >= 0 && i < static_cast< long >( hat.size() ) ? hat[ static_cast< size_t >( i ) ] : 0.0;
}

/// The kick's own peak, for matching the hat's level to it.
double kickPeak()
{
	static double peak = -1.0;
	if( peak < 0.0 )
	{
		peak = 0.0;
		for( int i = 0; i < kFeedRate; ++i )
			peak = std::max( peak, std::fabs( kickAt( static_cast< double >( i ) / kFeedRate ) ) );
	}
	return peak;
}

/// The harness's track, 120 BPM: a kick on the beat, a hat on the off-beat,
/// a 55 Hz bass note two beats in four. Peaks near 1.
double musicAt( double t )
{
	const double beat = std::fmod( t, 0.5 );
	const double bar  = std::fmod( t, 2.0 );
	double v          = 0.8 * kickAt( beat ) / kickPeak() + 0.25 * hatAt( std::fmod( t + 0.25, 0.5 ) );
	if( bar >= 1.0 )
		v += 0.25 * std::sin( 2.0 * kPi * 55.0 * t ) * fadeIn( bar - 1.0, 0.02 ) * fadeIn( 2.0 - bar, 0.02 );
	return v;
}

//---------------------------------------------------------------------------
// One plugin instance and the host-side structs it is handed.
//---------------------------------------------------------------------------
struct Instance
{
	WooferPlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	/// The feed's absolute sample clock at kFeedRate: samples [0, sent) have
	/// gone to feed(), as wftest's Feeder counts them.
	uint64_t sent = 0;
	Floats buffer;

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------

/// Before the first instance: give the plugin's log (Diag.cpp) somewhere to
/// go, a directory in the page's in-memory file system named through the
/// override Diag.cpp reads (its own `mkdir -p` goes through system(), which a
/// browser does not have); and WOOFER_NO_DEVICES=1, verify.sh's switch, so
/// AudioInput::Refresh never asks earshot for a device list (a build with no
/// capture backend would return none anyway).
EMSCRIPTEN_KEEPALIVE void wf_prepare()
{
	setenv( "WOOFER_LOG_DIR", "/woofer/logs", 1 );
	setenv( "WOOFER_NO_DEVICES", "1", 1 );
	mkdir( "/woofer", 0755 );
	mkdir( "/woofer/logs", 0755 );
}

EMSCRIPTEN_KEEPALIVE Instance* wf_new()
{
	return new Instance();
}

EMSCRIPTEN_KEEPALIVE void wf_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int wf_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int wf_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* wf_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int wf_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* wf_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float wf_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int wf_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* wf_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float wf_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE int wf_param_usage( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamUsage( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int wf_min_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMinInputs() );
}

EMSCRIPTEN_KEEPALIVE int wf_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int wf_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float wf_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

/// One frame: SetTime, then ProcessOpenGL into whatever the page has bound,
/// with one input of clipWidth x clipHeight whose GL name is `clip` (a texture
/// the page registered with emscripten's GL tables). 1 on success.
EMSCRIPTEN_KEEPALIVE int wf_process( Instance* instance, double seconds, int clip, int clipWidth, int clipHeight )
{
	instance->plugin.SetTime( seconds );
	instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
	instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
	instance->input.Handle                                 = static_cast< GLuint >( clip );
	instance->process.numInputTextures                     = 1;
	instance->process.inputTextures                        = instance->inputs;
	instance->process.HostFBO                              = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// The audio, as wftest hands it over. Each source is a fresh instance (the
// page's rule): a feed, once begun, is what earshot's Engine reads until a
// selectDevice, and nothing here selects.
//---------------------------------------------------------------------------

/// Begin the feed with no samples yet: earshot's feedBegin( 48000, 2 ), its
/// state Running and the frame's source the device path from here on.
EMSCRIPTEN_KEEPALIVE void wf_feed_begin( Instance* instance )
{
	static const float none[ 2 ] = { 0.0f, 0.0f };
	instance->plugin.FeedForTest( none, 0, kFeedRate, 2 );
}

/// wftest's track up to absolute sample `end` at kFeedRate, both channels
/// (`which` 0, wftest's default), the left only (1, `--left-only`) or zeros
/// (2, `--silent`): samples [sent, end) of the signal, as Feeder::Frame
/// makes them, into feed(). The page's `end` is its clock times 48 000,
/// rounded; wftest's is the frame's end, (frame + 1) x 48 000 / fps.
EMSCRIPTEN_KEEPALIVE int wf_feed_track( Instance* instance, double end, int which )
{
	const double wanted = std::floor( end + 0.5 );
	if( !( wanted > static_cast< double >( instance->sent ) ) )
		return 0;
	const int n = static_cast< int >( std::min( wanted - static_cast< double >( instance->sent ), 4.0 * kFeedRate ) );
	instance->buffer.resize( static_cast< size_t >( 2 * n ) );
	for( int k = 0; k < n; ++k )
	{
		const uint64_t at = instance->sent + static_cast< uint64_t >( k );
		float& l          = instance->buffer[ static_cast< size_t >( 2 * k ) ];
		float& r          = instance->buffer[ static_cast< size_t >( 2 * k + 1 ) ];
		const double t    = static_cast< double >( at ) / kFeedRate;
		if( which == 2 )
			l = r = 0.0f;
		else if( which == 1 )
		{
			l = static_cast< float >( musicAt( t ) );
			r = 0.0f;
		}
		else
			l = r = static_cast< float >( musicAt( t ) );
	}
	instance->plugin.FeedForTest( instance->buffer.data(), n, kFeedRate, 2 );
	instance->sent += static_cast< uint64_t >( n );
	return n;
}

/// A file the visitor chose, decoded by the page: `frames` interleaved frames
/// of `channels` at `rate`, into feed() as `wftest --wav` hands a WAV over.
EMSCRIPTEN_KEEPALIVE void wf_feed( Instance* instance, const float* interleaved, int frames, int rate, int channels )
{
	instance->plugin.FeedForTest( interleaved, frames, rate, channels );
}

/// wftest --host-fft: nothing fed; Resolume's FFT buffer only, with a kick's
/// spectrum on the beat, decaying -- low bins strong, a tail up the spectrum,
/// so both the bin law and the layout change the bands. `seconds` is the
/// host's time (wftest's frame / fps), written before that frame.
EMSCRIPTEN_KEEPALIVE void wf_host_fft( Instance* instance, double seconds )
{
	const double t = std::fmod( seconds, 0.5 );
	for( int e = 0; e < 64; ++e )
	{
		const double v = std::exp( -t / 0.12 ) * 0.8 * std::exp( -e / 6.0 ) + 0.05 * std::exp( -e / 30.0 );
		instance->plugin.SetParamElementValue( PT_AUDIO, static_cast< unsigned int >( e ), static_cast< float >( v ) );
	}
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one Woofer.cpp's ApplySettings, Advance and ProcessOpenGL apply to it.
// Light Angle and Light Turn come back in radians, as the plugin uses them.
// NaN where the plugin uses the host's 0..1 as it is (Ambient, Gloss, Mix),
// or an index.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double wf_convert( Instance* instance, int index, float v )
{
	switch( static_cast< unsigned int >( index ) )
	{
	case PT_BOX: return QtcFromParam( v );
	case PT_XMAX: return XmaxFromParam( v );
	case PT_OFFSET: return OffsetFromParam( v );
	case PT_BREAKUP: return BreakupFromParam( v );
	case PT_DRIVE: return DriveFromParam( v );
	case PT_SIZE: return SizeFromParam( v );
	case PT_CAMERA: return CameraFromParam( v );
	case PT_EXPOSURE: return ExposureFromParam( v );
	case PT_RIPPLE: return RippleFromParam( v );
	case PT_LIGHT_ANGLE: return LightAngleFromParam( v );
	case PT_LIGHT_TURN: return LightTurnFromParam( v );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// And back: the host's 0..1 for a value in the plugin's units, by the
/// inverse Controls.cpp pairs with each conversion (the one its constructor
/// sets the defaults with). The two light angles' inverses take DEGREES, as
/// Controls.cpp's do. NaN for a parameter with no conversion.
EMSCRIPTEN_KEEPALIVE double wf_param_from( Instance* instance, int index, double value )
{
	switch( static_cast< unsigned int >( index ) )
	{
	case PT_BOX: return ParamFromQtc( value );
	case PT_XMAX: return ParamFromXmax( value );
	case PT_OFFSET: return ParamFromOffset( value );
	case PT_BREAKUP: return ParamFromBreakup( value );
	case PT_DRIVE: return ParamFromDrive( value );
	case PT_SIZE: return ParamFromSize( value );
	case PT_CAMERA: return ParamFromCamera( value );
	case PT_EXPOSURE: return ParamFromExposure( value );
	case PT_RIPPLE: return ParamFromRipple( value );
	case PT_LIGHT_ANGLE: return ParamFromLightAngle( value );
	case PT_LIGHT_TURN: return ParamFromLightTurn( value );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

//---------------------------------------------------------------------------
// What the line under the picture reports: the plugin's own state, through
// the accessors its harness reads (ModelForTest(), AudioForTest(), HostNow()).
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE const char* wf_driver_model( Instance* instance )
{
	return instance->plugin.ModelForTest().Driver().model;
}
/// The coil's displacement at the newest sample, metres (+ toward the camera).
EMSCRIPTEN_KEEPALIVE double wf_x_now( Instance* instance )
{
	return instance->plugin.ModelForTest().Newest().x;
}
EMSCRIPTEN_KEEPALIVE double wf_max_abs_x( Instance* instance )
{
	return instance->plugin.ModelForTest().MaxAbsX();
}
EMSCRIPTEN_KEEPALIVE double wf_xmech( Instance* instance )
{
	return instance->plugin.ModelForTest().Xmech();
}
EMSCRIPTEN_KEEPALIVE double wf_xlin( Instance* instance )
{
	return instance->plugin.ModelForTest().Xlin();
}
EMSCRIPTEN_KEEPALIVE double wf_stop_hits( Instance* instance )
{
	return static_cast< double >( instance->plugin.ModelForTest().StopHits() );
}
/// Tap samples integrated since the model was configured (32 kHz).
EMSCRIPTEN_KEEPALIVE double wf_count( Instance* instance )
{
	return static_cast< double >( instance->plugin.ModelForTest().Count() );
}
/// V_ref: the volts at which full-scale DC holds the cone at Xlin in a Qtc
/// 0.707 box. Drive is decibels over it.
EMSCRIPTEN_KEEPALIVE double wf_reference_volts( Instance* instance )
{
	return DriverModel::ReferenceVolts( instance->plugin.ModelForTest().Driver() );
}
EMSCRIPTEN_KEEPALIVE int wf_samples_last_frame( Instance* instance )
{
	return instance->plugin.SamplesLastFrame();
}
/// 1 when the last frame's volts came from earshot's tap (the device path,
/// which feed() is), 0 when the fallback made them.
EMSCRIPTEN_KEEPALIVE int wf_from_device( Instance* instance )
{
	return instance->plugin.AudioForTest().FromDevice() ? 1 : 0;
}
/// earshot's CaptureState, as a number (Closed, Opening, Running, Silent,
/// Missing, Failed).
EMSCRIPTEN_KEEPALIVE int wf_capture_state( Instance* instance )
{
	return static_cast< int >( instance->plugin.AudioForTest().State() );
}
EMSCRIPTEN_KEEPALIVE const char* wf_capture_state_name( Instance* instance )
{
	return earshot::toString( instance->plugin.AudioForTest().State() );
}
/// The host's time as the plugin reads it, seconds (the wall clock until its
/// seconds-or-milliseconds vote settles), and the unit it settled on.
EMSCRIPTEN_KEEPALIVE double wf_host_now( Instance* instance )
{
	return instance->plugin.HostNow();
}
EMSCRIPTEN_KEEPALIVE double wf_clock_scale( Instance* instance )
{
	return instance->plugin.ClockScale();
}
/// Samples fed so far at 48 kHz, by wf_feed_track.
EMSCRIPTEN_KEEPALIVE double wf_fed( Instance* instance )
{
	return static_cast< double >( instance->sent );
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* wf_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

} // extern "C"
