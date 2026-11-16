/**
    wftest -- render Woofer offline, and measure what the cone is doing.

    It drives the REAL plugin class: the physics checks through the same
    Advance() that ProcessOpenGL calls (earshot's feed(), the tap, the
    integrator), with no GL at all; the picture checks through ProcessOpenGL
    itself, on a synthetic 60 fps clock, in a headless CGL context, reading
    back the host's output as floats. What it is measured against is
    independent of the integrator: the sealed box's small-signal transfer
    function in closed form, a second-order perturbation of the air spring,
    the symmetry of the model, a pinhole camera, the clip itself.

    **No audio device is ever opened or enumerated.** Before any plugin
    exists, main() installs a scripted capture backend (earshot's
    testing::CaptureBackend), which every plugin's earshot then uses and
    lists; audio reaches the plugin through earshot's feed().

        wftest --out /tmp/w.png            the defaults on the harness's card, a kick track, 3 s in
        wftest --list                      every parameter and its default
        wftest --pipe [--wav F.wav]        raw RGBA frames in on stdin, out on stdout, music from F
        wftest --film N [--wav F.wav]      N frames on the card, raw RGBA on stdout
        wftest --offline                   the checks and negative controls that need no GL context

    `--script` is the fleet's cue format: `frame  Parameter Name  value`
    lines, held before the first key and after the last. A STANDARD control is
    linear between its keys; an option or an event STEPS.

    WFTEST_RENDERER=software asks for Apple's software renderer by id: what a
    GPU-less CI runner falls back to.
*/

#include "Audio.h"
#include "Controls.h"
#include "Driver.h"
#include "Profile.h"
#include "Shaders.h"
#include "Woofer.h"

#include <earshot/testing.h>

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace woofer;

namespace
{
using Floats  = std::vector< float >;
using Doubles = std::vector< double >;
using Bytes   = std::vector< unsigned char >;
using Complex = std::complex< double >;

//---------------------------------------------------------------------------
// Reporting.
//---------------------------------------------------------------------------
int g_failures = 0;
int g_checks   = 0;
bool g_quiet   = false;///< --negative: only the verdicts

std::string fmt( const char* format, ... )
{
	char buffer[ 4096 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

void Check( bool condition, const std::string& message )
{
	++g_checks;
	if( !g_quiet )
		std::printf( "  %s  %s\n", condition ? "ok  " : "FAIL", message.c_str() );
	if( !condition )
		++g_failures;
}

void Note( const std::string& message )
{
	if( !g_quiet )
		std::printf( "        %s\n", message.c_str() );
}

int Verdict()
{
	std::printf( "\n  %s\n", g_failures == 0 ? "PASS" : "FAIL" );
	return g_failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// PNG. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( Bytes& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( Bytes& out, const char* type, const Bytes& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

/// `rgba` is floats, row 0 at the BOTTOM (GL's order); the file is written
/// top row first, which is the only place anything here flips.
bool writePng( const std::string& path, int width, int height, const Floats& rgba )
{
	Bytes raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		for( int x = 0; x < width; ++x )
			for( int c = 0; c < 4; ++c )
			{
				const float v = rgba[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
				raw.push_back( static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) ) );
			}
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	Bytes compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );
	Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	Bytes ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );
	FILE* file = std::fopen( path.c_str(), "wb" );
	if( !file )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// The context.
//---------------------------------------------------------------------------
bool g_software = false;

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute fallback[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	const char* renderer     = std::getenv( "WFTEST_RENDERER" );
	if( renderer != nullptr && std::strcmp( renderer, "software" ) == 0 )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		g_software = true;
		std::fprintf( stderr, "wftest: WFTEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( fallback, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

const char* kindName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_BUFFER: return "buffer";
	default: return "other";
	}
}

bool stepsBetweenCues( unsigned int type )
{
	return type == FF_TYPE_OPTION || type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT;
}

//---------------------------------------------------------------------------
// The scripted capture device. Nothing here talks to the OS: earshot's
// worker calls open() and close(), and a thread of ours delivers the audio,
// four times faster than real time.
//---------------------------------------------------------------------------
std::mutex g_deviceMutex;
std::vector< std::string > g_deviceNames = { "Test Mic", "Test Silent", "Test Slow" };
std::atomic< int > g_opens { 0 };

class ScriptedBackend final : public earshot::testing::CaptureBackend
{
public:
	~ScriptedBackend() override
	{
		close();
	}
	earshot::testing::OpenResult open( const char* name, earshot::testing::AudioSink& sink, char* openedName, char* error ) noexcept override
	{
		close();
		std::string wanted = name;
		if( wanted == earshot::kDefaultDevice )
			wanted = "Test Mic";
		{
			std::lock_guard< std::mutex > lock( g_deviceMutex );
			if( std::find( g_deviceNames.begin(), g_deviceNames.end(), wanted ) == g_deviceNames.end() )
			{
				std::snprintf( error, 128, "%s is not connected (scripted)", wanted.c_str() );
				return earshot::testing::OpenResult::NotConnected;
			}
		}
		if( wanted == "Test Slow" )
			std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
		std::snprintf( openedName, 128, "%s", wanted.c_str() );
		++g_opens;
		sink.begin( 48000, 2 );
		running      = true;
		const bool silent = wanted == "Test Silent";
		worker       = std::thread( [ this, &sink, silent ] {
            std::vector< float > block( 2 * 480 );
            uint64_t n = 0;
            while( running )
            {
                for( int k = 0; k < 480; ++k, ++n )
                {
                    const float v = silent ? 0.0f : static_cast< float >( 0.5 * std::sin( 2.0 * 3.14159265358979 * 50.0 * n / 48000.0 ) );
                    block[ 2 * k ] = block[ 2 * k + 1 ] = v;
                }
                sink.deliver( block.data(), 480, 2 );
                std::this_thread::sleep_for( std::chrono::microseconds( 2500 ) );
            }
		} );
		return earshot::testing::OpenResult::Opened;
	}
	void close() noexcept override
	{
		running = false;
		if( worker.joinable() )
			worker.join();
	}
	std::vector< earshot::DeviceInfo > enumerate() noexcept override
	{
		std::lock_guard< std::mutex > lock( g_deviceMutex );
		std::vector< earshot::DeviceInfo > list;
		for( const std::string& name : g_deviceNames )
			list.push_back( { name, 2, false } );
		return list;
	}

private:
	std::atomic< bool > running { false };
	std::thread worker;
};

std::unique_ptr< earshot::testing::CaptureBackend > scriptedBackend()
{
	return std::make_unique< ScriptedBackend >();
}

//---------------------------------------------------------------------------
// Signals: mono, as functions of the sample index at a rate.
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
// WAV: PCM 16/24/32 or float 32, any rate and channel count (the first two
// channels are kept).
//---------------------------------------------------------------------------
struct Wav
{
	int rate     = 0;
	int channels = 0;
	Floats left, right;
};

bool readWav( const std::string& path, Wav& wav, std::string& error )
{
	std::ifstream file( path, std::ios::binary );
	if( !file )
	{
		error = "cannot open " + path;
		return false;
	}
	Bytes data( ( std::istreambuf_iterator< char >( file ) ), std::istreambuf_iterator< char >() );
	auto u32 = [ & ]( size_t at ) { return static_cast< uint32_t >( data[ at ] | ( data[ at + 1 ] << 8 ) | ( data[ at + 2 ] << 16 ) | ( data[ at + 3 ] << 24 ) ); };
	auto u16 = [ & ]( size_t at ) { return static_cast< uint32_t >( data[ at ] | ( data[ at + 1 ] << 8 ) ); };
	if( data.size() < 12 || std::memcmp( data.data(), "RIFF", 4 ) != 0 || std::memcmp( data.data() + 8, "WAVE", 4 ) != 0 )
	{
		error = path + " is not a RIFF WAVE file";
		return false;
	}
	int format = 0, bits = 0;
	size_t at  = 12;
	while( at + 8 <= data.size() )
	{
		const uint32_t size = u32( at + 4 );
		if( std::memcmp( data.data() + at, "fmt ", 4 ) == 0 && at + 8 + 16 <= data.size() )
		{
			format       = static_cast< int >( u16( at + 8 ) );
			wav.channels = static_cast< int >( u16( at + 10 ) );
			wav.rate     = static_cast< int >( u32( at + 12 ) );
			bits         = static_cast< int >( u16( at + 22 ) );
			if( format == 0xFFFE && size >= 40 )
				format = static_cast< int >( u16( at + 8 + 24 ) );
		}
		else if( std::memcmp( data.data() + at, "data", 4 ) == 0 )
		{
			if( wav.channels <= 0 || ( format != 1 && format != 3 ) )
			{
				error = path + ": only PCM or float WAV";
				return false;
			}
			// A header claiming 0 or 8 bits would make a zero-sized frame and an
			// endless loop below: refuse anything but the three depths read here.
			if( bits != 16 && bits != 24 && bits != 32 )
			{
				error = path + ": only 16, 24 or 32-bit WAV";
				return false;
			}
			const int bytes        = bits / 8;
			const size_t frameSize = static_cast< size_t >( bytes ) * wav.channels;
			const size_t end       = std::min< size_t >( data.size(), at + 8 + size );
			for( size_t p = at + 8; p + frameSize <= end; p += frameSize )
				for( int c = 0; c < std::min( 2, wav.channels ); ++c )
				{
					const size_t q = p + static_cast< size_t >( c ) * bytes;
					float v        = 0.0f;
					if( format == 3 && bits == 32 )
						std::memcpy( &v, &data[ q ], 4 );
					else if( bits == 16 )
						v = static_cast< int16_t >( u16( q ) ) / 32768.0f;
					else if( bits == 24 )
						v = static_cast< float >( static_cast< int32_t >( ( data[ q ] << 8 ) | ( data[ q + 1 ] << 16 ) | ( data[ q + 2 ] << 24 ) ) / 2147483648.0 );
					else if( bits == 32 )
						v = static_cast< float >( static_cast< int32_t >( u32( q ) ) / 2147483648.0 );
					( c == 0 ? wav.left : wav.right ).push_back( v );
				}
			if( wav.channels == 1 )
				wav.right = wav.left;
			return !wav.left.empty();
		}
		at += 8 + size + ( size & 1 );
	}
	error = path + ": no data chunk";
	return false;
}

//---------------------------------------------------------------------------
// Pictures. Every one is rows BOTTOM first (GL's order), as uploaded.
//---------------------------------------------------------------------------
uint32_t pcg( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
double hash01( uint32_t a, uint32_t b = 0 )
{
	return pcg( a * 2654435761u ^ pcg( b + 0x9e3779b9u ) ) * ( 1.0 / 4294967296.0 );
}

/// The look card (schlieren's, by way of fountain): a sunset sky, a sun, a
/// skyline, a stripe of saturated bars and a white title block.
Floats buildCard( int width, int height )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	const double s = std::min( width, height );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 - 0.5 * width ) / s, v = ( y + 0.5 - 0.5 * height ) / s;
			const double k = std::clamp( 0.5 + v, 0.0, 1.0 );
			double r = 0.95 - 0.8 * k, g = 0.45 - 0.3 * k, b = 0.25 + 0.5 * k;
			const double d = std::sqrt( ( u + 0.25 ) * ( u + 0.25 ) + ( v + 0.02 ) * ( v + 0.02 ) );
			if( d < 0.2 )
			{
				r = 1.0;
				g = 0.85 - 0.5 * d;
				b = 0.35;
			}
			const int block  = static_cast< int >( std::floor( ( u + 2.0 ) * 11.0 ) );
			const double top = -0.12 + 0.16 * hash01( static_cast< uint32_t >( block ), 7 );
			if( v < top )
			{
				r = g = b = 0.05;
				const int wx = static_cast< int >( std::floor( ( u + 2.0 ) * 70.0 ) ), wy = static_cast< int >( std::floor( v * 60.0 + 100.0 ) );
				if( ( wx % 3 ) != 0 && ( wy % 2 ) == 0 && hash01( static_cast< uint32_t >( wx * 131 + wy ), 9 ) > 0.55 )
				{
					r = 1.0;
					g = 0.9;
					b = 0.55;
				}
			}
			if( v < -0.36 )
			{
				const int bar               = std::clamp( static_cast< int >( std::floor( ( x + 0.5 ) / width * 7.0 ) ), 0, 6 );
				const double bars[ 7 ][ 3 ] = { { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 1 }, { 0, 1, 0 }, { 1, 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 } };
				r = 0.75 * bars[ bar ][ 0 ];
				g = 0.75 * bars[ bar ][ 1 ];
				b = 0.75 * bars[ bar ][ 2 ];
			}
			if( std::fabs( u - 0.42 ) < 0.28 && std::fabs( v - 0.3 ) < 0.06 )
				r = g = b = 0.95;
			float* o = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			o[ 0 ]   = static_cast< float >( r );
			o[ 1 ]   = static_cast< float >( g );
			o[ 2 ]   = static_cast< float >( b );
			o[ 3 ]   = 1.0f;
		}
	return card;
}

Floats flatCard( int width, int height, float r, float g, float b, float a = 1.0f )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( size_t i = 0; i < card.size(); i += 4 )
	{
		card[ i ]     = r;
		card[ i + 1 ] = g;
		card[ i + 2 ] = b;
		card[ i + 3 ] = a;
	}
	return card;
}

GLuint makeTexture( int width, int height, const float* pixels )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	//Linear, as a host's clip texture is.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

//---------------------------------------------------------------------------
// Parameters by display name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	unsigned int type;
};

std::vector< NamedParameter > listParameters( WooferPlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ), plugin.GetParamType( i ) } );
	}
	return list;
}

/// An element's stored value, as the host reads it (FF_GET_PARAM_ELEMENT_DEFAULT).
float elementValue( WooferPlugin& plugin, unsigned int index, unsigned int element )
{
	const FFMixed mixed = plugin.GetParamElementDefault( index, element );
	float value         = 0.0f;
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

/// An option's element by its name: its stored value (a device key for Audio
/// Input, the index otherwise).
bool optionValue( WooferPlugin& plugin, unsigned int index, const std::string& value, float& out )
{
	for( unsigned int e = 0; e < plugin.GetNumParamElements( index ); ++e )
		if( value == plugin.GetParamElementName( index, e ) )
		{
			out = index == PT_INPUT ? elementValue( plugin, index, e ) : static_cast< float >( e );
			return true;
		}
	return false;
}

/// `Name=Value`. The value is a number, or for an option the name of one of
/// its elements; anything else is refused, never read as 0 (polyhedral's trap).
bool applySetting( WooferPlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	for( const NamedParameter& parameter : listParameters( plugin ) )
		if( parameter.name == name )
		{
			if( parameter.type == FF_TYPE_TEXT || parameter.type == FF_TYPE_BUFFER )
			{
				error = name + " cannot be set from here";
				return false;
			}
			float v = 0.0f;
			if( parameter.type == FF_TYPE_OPTION && optionValue( plugin, parameter.index, value, v ) )
			{
				plugin.SetFloatParameter( parameter.index, v );
				return true;
			}
			char* end       = nullptr;
			const float got = std::strtof( value.c_str(), &end );
			if( value.empty() || end == value.c_str() || *end != '\0' || !std::isfinite( got ) )
			{
				error = "'" + value + "' is not a number" + ( parameter.type == FF_TYPE_OPTION ? " or an option of " + name : "" );
				return false;
			}
			plugin.SetFloatParameter( parameter.index, got );
			return true;
		}
	error = "no parameter called '" + name + "'";
	return false;
}

using Track = std::vector< std::pair< int, float > >;
struct Cue
{
	Track track;
	bool ramp;
};

float valueAt( const Track& track, int frame, bool ramp )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 0; i + 1 < track.size(); ++i )
	{
		const auto& a = track[ i ];
		const auto& b = track[ i + 1 ];
		if( frame >= a.first && frame < b.first )
		{
			if( !ramp )
				return a.second;
			const float t = static_cast< float >( frame - a.first ) / static_cast< float >( b.first - a.first );
			return a.second + ( b.second - a.second ) * t;
		}
	}
	return track.back().second;
}

/// A cue line's name is the longest run of words that is a parameter, and the
/// rest is its value: a number, or an option's name (honeydew's rule).
bool bindScript( WooferPlugin& plugin, std::istream& in, const std::string& path, std::map< unsigned int, Cue >& out, std::string& error )
{
	const std::vector< NamedParameter > known = listParameters( plugin );
	std::string line;
	int lineNumber = 0;
	while( std::getline( in, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream words( line );
		int frame = 0;
		if( !( words >> frame ) )
			continue;
		std::vector< std::string > parts;
		std::string word;
		while( words >> word )
			parts.push_back( word );
		const NamedParameter* found = nullptr;
		size_t used                 = 0;
		for( size_t n = parts.size() > 0 ? parts.size() - 1 : 0; n >= 1 && found == nullptr; --n )
		{
			std::string name = parts[ 0 ];
			for( size_t i = 1; i < n; ++i )
				name += " " + parts[ i ];
			for( const NamedParameter& p : known )
				if( p.name == name )
				{
					found = &p;
					used  = n;
				}
		}
		if( found == nullptr )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": names no parameter (try --list)";
			return false;
		}
		std::string value = parts[ used ];
		for( size_t i = used + 1; i < parts.size(); ++i )
			value += " " + parts[ i ];
		float v      = 0.0f;
		bool matched = found->type == FF_TYPE_OPTION && optionValue( plugin, found->index, value, v );
		if( !matched )
		{
			char* end = nullptr;
			v         = std::strtof( value.c_str(), &end );
			if( value.empty() || end == value.c_str() || *end != '\0' )
			{
				error = path + ":" + std::to_string( lineNumber ) + ": '" + value + "' is not a number";
				return false;
			}
		}
		Cue& cue = out[ found->index ];
		cue.ramp = !stepsBetweenCues( found->type );
		cue.track.emplace_back( frame, v );
	}
	for( auto& entry : out )
		std::stable_sort( entry.second.track.begin(), entry.second.track.end(),
		                  []( const std::pair< int, float >& a, const std::pair< int, float >& b ) { return a.first < b.first; } );
	return true;
}

//---------------------------------------------------------------------------
// Audio into a plugin, a frame at a time, on the absolute sample clock: frame
// k at F fps gets samples [round(k R/F), round((k+1) R/F)) of the signal.
//---------------------------------------------------------------------------
using Signal = std::function< void( double t, float& l, float& r ) >;

struct Feeder
{
	Signal signal;
	const Wav* wav = nullptr;///< instead of `signal`: the file at its own rate, then silence
	int rate      = kFeedRate;
	uint64_t sent = 0;
	Floats buffer;

	void Frame( WooferPlugin& plugin, int frame, double fps );
};

void Feeder::Frame( WooferPlugin& plugin, int frame, double fps )
	{
		if( !signal && wav == nullptr )
			return;
		const uint64_t end = static_cast< uint64_t >( std::llround( ( frame + 1 ) * static_cast< double >( rate ) / fps ) );
		if( end <= sent )
			return;
		const int n = static_cast< int >( end - sent );
		buffer.resize( static_cast< size_t >( 2 * n ) );
		for( int k = 0; k < n; ++k )
		{
			const uint64_t at = sent + static_cast< uint64_t >( k );
			float& l          = buffer[ static_cast< size_t >( 2 * k ) ];
			float& r          = buffer[ static_cast< size_t >( 2 * k + 1 ) ];
			if( wav != nullptr )
			{
				l = at < wav->left.size() ? wav->left[ at ] : 0.0f;
				r = at < wav->right.size() ? wav->right[ at ] : 0.0f;
			}
			else
				signal( static_cast< double >( at ) / rate, l, r );
		}
		plugin.FeedForTest( buffer.data(), n, rate, 2 );
		sent = end;
	}

Signal mono( std::function< double( double ) > f )
{
	return [ f ]( double t, float& l, float& r ) { l = r = static_cast< float >( f( t ) ); };
}

struct HarnessPerturb
{
	Perturb model;
	ProfileSettings profile;///< only its negative-control flags are read
	int hooks             = 0;
	bool resetOnResize    = false;
	bool fallbackFromTap  = false;
	bool menuByIndex      = false;
	bool clockAsSeconds   = false;
};

/// Every negative control onto one plugin.
void applyPerturb( WooferPlugin& plugin, const HarnessPerturb& hp )
{
	plugin.SetPerturbForTest( hp.model, hp.profile );
	plugin.SetHooksForTest( hp.hooks );
	plugin.SetResetOnResizeForTest( hp.resetOnResize );
	plugin.AudioForTest().fallbackFromTap = hp.fallbackFromTap;
	plugin.AudioForTest().menuByIndex     = hp.menuByIndex;
}

/// New tap samples since `seen`, oldest first, from the model's trace.
/// `lost` null: a gap aborts. Otherwise the gap is counted there and skipped.
void collectTrace( DriverModel& model, uint64_t& seen, Doubles& x, std::vector< Sample >* states, uint64_t* lost = nullptr )
{
	//A reset model (the resize check's negative control) counts from 0 again:
	//everything since is new. Unsigned subtraction would wrap to ~2^64 and
	//push until memory ran out -- it did, at 36 GB, and the whole --negative
	//run died with no output (stdout to a pipe is block-buffered).
	if( model.Count() < seen )
		seen = 0;
	uint64_t added = model.Count() - seen;
	//More than the ring holds since the last look and the oldest are gone:
	//At() would clamp and repeat one. The fallback's frames are at most
	//kMaxFrameDelta (8000 samples), but the device path takes everything the
	//tap queued, up to a second: the scripted backend runs at 4x real time,
	//so a 50 ms software-renderer frame brings 6400 and one slow frame more
	//than the ring (8320 seen). Only a rig that says so may skip; every check
	//that needs each sample still aborts.
	if( lost && added > static_cast< uint64_t >( DriverModel::kTraceLength ) )
	{
		*lost += added - DriverModel::kTraceLength;
		added = DriverModel::kTraceLength;
	}
	if( added > static_cast< uint64_t >( DriverModel::kTraceLength ) )
	{
		std::fprintf( stderr, "wftest: %llu samples since the last look, more than the trace's %d\n", static_cast< unsigned long long >( added ),
		              DriverModel::kTraceLength );
		std::abort();
	}
	for( uint64_t k = added; k > 0; --k )
	{
		const Sample s = model.At( static_cast< double >( k - 1 ) );
		x.push_back( s.x );
		if( states )
			states->push_back( s );
	}
	seen = model.Count();
}

/// The physics without GL: the plugin's own Advance(), fed frame by frame,
/// recording every tap sample's volts and every trace sample's state.
struct Physics
{
	std::unique_ptr< WooferPlugin > plugin = std::make_unique< WooferPlugin >();
	Feeder feeder;
	double fps = 60.0;
	int frame  = 0;
	Doubles volts, x;
	std::vector< Sample > states;
	bool keepStates = false;

	void Set( unsigned int id, float v )
	{
		plugin->SetFloatParameter( id, v );
	}
	void Apply( const HarnessPerturb& hp )
	{
		applyPerturb( *plugin, hp );
	}
	/// Settle the controls into the model (no audio, no time).
	void Prime()
	{
		plugin->AdvanceForTest( 0.0, 0.0 );
		seen = Model().Count();
	}
	DriverModel& Model()
	{
		return plugin->ModelForTest();
	}
	/// Run until `seconds` of host time have passed.
	void RunFor( double seconds )
	{
		const int frames = static_cast< int >( std::llround( seconds * fps ) );
		for( int i = 0; i < frames; ++i )
			Step();
	}
	void Step()
	{
		feeder.Frame( *plugin, frame, fps );
		plugin->AdvanceForTest( frame / fps, frame > 0 ? 1.0 / fps : 0.0 );
		++frame;
		for( float v : plugin->VoltsForTest() )
			volts.push_back( v );
		collectTrace( Model(), seen, x, keepStates ? &states : nullptr );
	}
	uint64_t seen = 0;
};

/// The sealed box's small-signal transfer function X/V, with Le (SPEC-woofer.md).
Complex closedForm( const Preset& p, double alpha, double hz, bool withLe = true )
{
	const Complex s( 0.0, 2.0 * kPi * hz );
	const double kt = ( 1.0 + alpha ) / p.Cms;
	return p.Bl / ( ( p.Re + ( withLe ? s * p.Le : Complex( 0.0 ) ) ) * ( p.Mms * s * s + p.Rms * s + kt ) + p.Bl * p.Bl * s );
}

/// One DFT bin of `v[ from .. from + n )` at `hz` (sample rate `rate`).
Complex dftBin( const Doubles& v, size_t from, size_t n, double hz, double rate )
{
	Complex sum = 0.0;
	for( size_t k = 0; k < n; ++k )
	{
		const double a = -2.0 * kPi * hz * static_cast< double >( from + k ) / rate;
		sum += v[ from + k ] * Complex( std::cos( a ), std::sin( a ) );
	}
	return sum * ( 2.0 / static_cast< double >( n ) );
}

double meanOf( const Doubles& v, size_t from, size_t n )
{
	double s = 0.0;
	for( size_t k = 0; k < n; ++k )
		s += v[ from + k ];
	return s / static_cast< double >( n );
}

//---------------------------------------------------------------------------
// A rig: the real plugin through ProcessOpenGL, a float output framebuffer,
// a synthetic clock, and audio fed a frame at a time.
//---------------------------------------------------------------------------
struct Rig
{
	WooferPlugin plugin;
	int width = 0, height = 0;
	GLuint sourceTexture = 0, outputTexture = 0, outputFBO = 0;
	int frame  = 0;
	double fps = 60.0;
	/// What SetTime is told for frame f: timeOffset + timeScale x f / fps.
	double timeOffset = 0.0, timeScale = 1.0;
	Feeder feeder;
	std::function< void( int ) > beforeFrame;
	/// Tell the plugin the clock is in seconds at Init (false: let it vote).
	bool presetClock = true;
	/// Every tap sample's x since Init, and the most samples any frame took.
	Doubles x;
	uint64_t seen = 0;
	int samples   = 0;///< tap samples integrated, frames after the first
	/// Tolerate frames bigger than the trace (the device path), counting the
	/// samples x never saw here. False: such a frame aborts.
	bool gapsAllowed = false;
	uint64_t lost    = 0;

	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };

	~Rig()
	{
		plugin.DeInitGL();
		release();
	}

	void release()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
		outputFBO = outputTexture = sourceTexture = 0;
	}

	bool attach( int w, int h, const Floats* picture )
	{
		width         = w;
		height        = h;
		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;
		process.HostFBO   = outputFBO;
		const Floats card = picture ? *picture : buildCard( width, height );
		sourceTexture     = makeTexture( width, height, card.data() );
		inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
		inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
		inputStruct.Handle                              = sourceTexture;
		inputs[ 0 ]                                     = &inputStruct;
		process.numInputTextures                        = 1;
		process.inputTextures                           = inputs;
		return true;
	}

	bool Init( int w, int h, const Floats* picture = nullptr )
	{
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( w );
		viewport.height             = static_cast< FFUInt32 >( h );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/woofer\n" );
			return false;
		}
		if( presetClock )
			plugin.SetClockScaleForTest( 1.0 );
		return attach( w, h, picture );
	}

	/// The host's raster changes under a running instance: no InitGL.
	bool Resize( int w, int h, const Floats* picture = nullptr )
	{
		release();
		return attach( w, h, picture );
	}

	void Upload( const Floats& picture )
	{
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, picture.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void Set( unsigned int id, float value )
	{
		plugin.SetFloatParameter( id, value );
	}

	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			if( beforeFrame )
				beforeFrame( frame );
			feeder.Frame( plugin, frame, fps );
			plugin.SetTime( timeOffset + timeScale * static_cast< double >( frame ) / fps );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
			if( frame > 1 )
				samples += plugin.SamplesLastFrame();
			collectTrace( plugin.ModelForTest(), seen, x, nullptr, gapsAllowed ? &lost : nullptr );
		}
		return true;
	}

	void Apply( const HarnessPerturb& hp )
	{
		applyPerturb( plugin, hp );
	}

	/// The output at a pixel (bottom-left origin), as 8-bit codes.
	void CodeAt( const Floats& image, int px, int py, double out[ 4 ] ) const
	{
		const float* p = &image[ ( static_cast< size_t >( py ) * width + px ) * 4 ];
		for( int c = 0; c < 4; ++c )
			out[ c ] = p[ c ] * 255.0;
	}

	/// A block of the output, rows bottom first.
	Floats Region( int x0, int y0, int w, int h ) const
	{
		Floats pixels( static_cast< size_t >( w ) * h * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( x0, y0, w, h, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}

	Floats Output() const
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}
};

const float* pixelAt( const Floats& image, int width, int x, int y )
{
	return &image[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
}

double linearOf( double code )
{
	//IEC 61966-2-1, the harness's own.
	return code <= 0.04045 ? code / 12.92 : std::pow( ( code + 0.055 ) / 1.055, 2.4 );
}

//===========================================================================
// The checks are in checks.inc; see checks().
//===========================================================================
using CheckFn = int ( * )( const HarnessPerturb& );

struct Raster
{
	int w, h;
};
std::vector< Raster > kRasters = { { 1280, 720 }, { 320, 180 } };

#include "checks.inc"

} // namespace

#include "run.inc"
