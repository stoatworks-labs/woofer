#include "Audio.h"

#include "Diag.h"
#include "Driver.h"

#include <earshot/testing.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace woofer
{
namespace
{
BackendFactory g_factory = nullptr;

/// The fallback's bands: edges in Hz. Linear 64 bins at 44.1 kHz put bin 0
/// (172 Hz at its centre) in the first band.
constexpr float kBandEdges[ 5 ] = { 20.0f, 200.0f, 800.0f, 3200.0f, 12800.0f };
constexpr int kBands            = 4;
/// Most samples one frame takes from the tap (a second); older ones go.
constexpr int kMaxPull = DriverModel::kTapRate;
/// The fallback's longest frame, in seconds.
constexpr double kMaxFallbackStep = 0.25;

/// WOOFER_NO_DEVICES=1: never ask the OS for its devices (verify.sh sets it
/// for oxbow, so no step of verification talks to CoreAudio at all).
bool devicesDisabled()
{
	const char* v = std::getenv( "WOOFER_NO_DEVICES" );
	return v != nullptr && *v != '\0' && std::strcmp( v, "0" ) != 0;
}

earshot::Config makeConfig()
{
	earshot::Config c;
	c.maxChannels = 2;
	c.tapRate     = DriverModel::kTapRate;
	c.bandCount   = kBands;
	for( int i = 0; i <= kBands; ++i )
		c.bandEdges[ i ] = kBandEdges[ i ];
	return c;
}
} // namespace

void AudioInput::SetBackendFactoryForTest( BackendFactory factory )
{
	g_factory = factory;
}

AudioInput::AudioInput() :
	left( kMaxPull ), right( kMaxPull )
{
	if( g_factory != nullptr )
		engine = std::make_unique< earshot::Engine >( makeConfig(), g_factory() );
	else
		engine = std::make_unique< earshot::Engine >( makeConfig() );
	if( !engine->ok() )
		diag::error( "earshot could not allocate: the cone will not move" );
	Refresh();
}

AudioInput::~AudioInput() = default;

void AudioInput::Refresh()
{
	slots.clear();
	slots.push_back( { kHostFftLabel, "", 0.0f } );
	slots.push_back( { kDefaultLabel, earshot::kDefaultDevice, earshot::deviceKey( earshot::kDefaultDevice ) } );
	std::vector< earshot::DeviceInfo > devices;
	if( g_factory != nullptr )
	{
		//A throwaway backend of the harness's kind: it lists, it opens nothing.
		if( auto lister = g_factory() )
			devices = lister->enumerate();
	}
	else if( !devicesDisabled() )
		devices = earshot::listDevices();
	for( const earshot::DeviceInfo& d : devices )
	{
		if( static_cast< int >( slots.size() ) >= kInputSlots )
			break;
		const float key = earshot::deviceKey( d.name.c_str() );
		seen[ key ]     = d.name;
		slots.push_back( { d.name.substr( 0, 30 ), d.name, key } );
	}
	while( static_cast< int >( slots.size() ) < kInputSlots )
		slots.push_back( { kEmptySlot, "", 0.0f } );
	if( menuByIndex )
		for( size_t i = 0; i < slots.size(); ++i )
			slots[ i ].value = static_cast< float >( i );
	diag::info( "audio inputs: " + std::to_string( devices.size() ) + " device(s) listed" );
}

void AudioInput::Select( float value )
{
	if( value == applied )
		return;
	applied = value;
	feeding = false;// any select ends a feed
	std::string name;
	if( menuByIndex )
	{
		const int index = std::clamp( static_cast< int >( std::lround( value ) ), 0, static_cast< int >( slots.size() ) - 1 );
		name            = slots[ static_cast< size_t >( index ) ].name;
	}
	else if( value == 0.0f )
		name.clear();
	else if( value == earshot::deviceKey( earshot::kDefaultDevice ) )
		name = earshot::kDefaultDevice;
	else
	{
		const auto known = seen.find( value );
		//A key with no device met in this process: ask for a name that cannot
		//match, so earshot says Missing (never the default). Rescan resolves it
		//once the device is back.
		name = known != seen.end() ? known->second : "(device " + std::to_string( static_cast< long >( value ) ) + " not present)";
	}
	engine->selectDevice( name.c_str() );
	diag::info( "audio input: " + ( name.empty() ? std::string( "host FFT" ) : name ) );
}

const earshot::Frame& AudioInput::Frame() const
{
	return engine->frame();
}

earshot::CaptureState AudioInput::State() const
{
	return engine->captureState();
}

void AudioInput::Feed( const float* interleaved, int frames, int rate, int channels )
{
	if( !feeding )
	{
		engine->feedBegin( rate, channels );
		feeding = true;
	}
	engine->feed( interleaved, frames );
}

int AudioInput::Pull( double hostSeconds, double dt, const float* bins, int binCount, Channel channel, FftLaw law, double volts,
                      std::vector< float >& out )
{
	if( static_cast< int >( law ) != appliedLaw )
	{
		earshot::Config c       = engine->config();
		const bool power        = law == FftLaw::LinearPower || law == FftLaw::LogPower;
		const bool logarithmic  = law == FftLaw::LogMagnitude || law == FftLaw::LogPower;
		c.hostBins.value        = power ? earshot::HostBins::Value::Power : earshot::HostBins::Value::Magnitude;
		c.hostBins.layout       = logarithmic ? earshot::HostBins::Layout::Log : earshot::HostBins::Layout::Linear;
		engine->configure( c );
		appliedLaw = static_cast< int >( law );
	}

	const earshot::Frame& f = engine->update( hostSeconds, bins, binCount );
	out.clear();
	lastFromDevice = f.source == earshot::Source::Device;

	if( lastFromDevice || fallbackFromTap )
	{
		//The real path: every tap sample since the last frame, once. If more
		//than a second queued (a stalled host), the oldest are dropped.
		int n = engine->readTap( left.data(), right.data(), kMaxPull );
		while( f.tapAvailable > 0 && n == kMaxPull )
		{
			const int more = engine->readTap( left.data(), right.data(), kMaxPull );
			if( more <= 0 )
				break;
			n = more;
		}
		out.resize( static_cast< size_t >( n ) );
		for( int k = 0; k < n; ++k )
		{
			const float s = channel == Channel::Left ? left[ k ] : channel == Channel::Right ? right[ k ] : 0.5f * ( left[ k ] + right[ k ] );
			out[ static_cast< size_t >( k ) ] = static_cast< float >( s * volts );
		}
		if( lastFromDevice )
		{
			std::fill( std::begin( prevLevel ), std::end( prevLevel ), 0.0 );
			return n;
		}
	}

	//The host FFT, or nothing: the host clock says how many samples.
	carry += std::clamp( dt, 0.0, kMaxFallbackStep ) * DriverModel::kTapRate;
	const int n = static_cast< int >( std::floor( carry ) );
	carry -= n;
	if( fallbackFromTap )
		return static_cast< int >( out.size() );
	out.assign( static_cast< size_t >( n ), 0.0f );
	if( f.source != earshot::Source::Host || n <= 0 )
	{
		std::fill( std::begin( prevLevel ), std::end( prevLevel ), 0.0 );
		return n;
	}
	for( int b = 0; b < std::min( kBands, f.bandCount ); ++b )
	{
		const double hz    = std::sqrt( static_cast< double >( kBandEdges[ b ] ) * kBandEdges[ b + 1 ] );
		const double step  = 2.0 * kPi * hz / DriverModel::kTapRate;
		const double level = std::clamp( static_cast< double >( f.bands[ b ].level ), 0.0, 1.0 );
		for( int k = 0; k < n; ++k )
		{
			const double amp = prevLevel[ b ] + ( level - prevLevel[ b ] ) * ( k + 1.0 ) / n;
			out[ static_cast< size_t >( k ) ] += static_cast< float >( amp * std::sin( phase[ b ] ) * volts );
			phase[ b ] += step;
		}
		phase[ b ] = std::fmod( phase[ b ], 2.0 * kPi );
		prevLevel[ b ] = level;
	}
	return n;
}

} // namespace woofer
