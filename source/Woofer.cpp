#include "Woofer.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace woofer
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Frames of advancing host time after which an unsettled vote is decided by
/// the size of the host's step alone (fountain's: a host that renders faster
/// than real time -- oxbow's selftest, wftest -- never wins the vote).
constexpr int kClockPatience = 30;
/// Host seconds one frame may advance the fallback by.
constexpr double kMaxFrameDelta = 0.25;
/// How long a stop hit lights the lamp white, in audio seconds.
constexpr double kHitFlash = 0.080;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

double microsSince( std::chrono::steady_clock::time_point t0 )
{
	using namespace std::chrono;
	return duration_cast< duration< double, std::micro > >( steady_clock::now() - t0 ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

bool same( const DriverSettings& a, const DriverSettings& b )
{
	return a.preset == b.preset && a.qtc == b.qtc && a.xmaxScale == b.xmaxScale && a.offsetFraction == b.offsetFraction
	       && a.breakupScale == b.breakupScale;
}
bool same( const ProfileSettings& a, const ProfileSettings& b )
{
	return a.size == b.size && a.camera == b.camera && a.ripple == b.ripple && a.ortho == b.ortho && a.printMaterial == b.printMaterial
	       && a.stretchRoll == b.stretchRoll;
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
WooferPlugin::WooferPlugin() :
	table( static_cast< size_t >( Profile::kBins ) * Profile::kFloatsPerBin * Profile::kRows )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );
	//The fallback's sample count and the lamp's blink are in seconds.
	SetTimeSupported( true );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Devices:
		{
			//The device menu (spasis's shape, earshot's keys). The range covers
			//the keys (23 bits) for any host that clamps an option to it.
			SetOptionParamInfo( id, info.name, kInputSlots, 0.0f );
			const auto& slots = audio.Slots();
			for( int i = 0; i < kInputSlots; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), slots[ static_cast< size_t >( i ) ].label.c_str(),
				                     slots[ static_cast< size_t >( i ) ].value );
			SetParamRange( id, 0.0f, 8388608.0f );
			break;
		}
		case Kind::Event: SetParamInfo( id, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Buffer:
			//Resolume's FFT: an audio-source picker, 64 bins written every frame.
			//The fallback's only input.
			SetBufferParamInfo( id, info.name, 64, FF_USAGE_FFT );
			for( unsigned int i = 0; i < 64; ++i )
				SetParamElementInfo( id, i, "", 0.0f );
			break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block, last. Inline: SetParamInfo is protected on CFFGLPlugin.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint WooferPlugin::where( const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	for( const auto& entry : uniformCache )
		if( entry.first == name )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( name, location );
	return location;
}

FFResult WooferPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );
	const std::string vertex   = shaders::AssembleVertex();
	const std::string fragment = shaders::AssembleFragment();
	if( !shader.Compile( vertex.c_str(), fragment.c_str() ) )
	{
		diag::error( "the shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Woofer: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	//Allocate before binding anything (the fleet's rule), and put back what
	//was bound: Scoped* bindings clear to 0 rather than restore.
	SavedGLState saved;
	saved.Capture();
	glGenVertexArrays( 1, &vao );
	glGenTextures( 1, &tableTexture );
	glBindTexture( GL_TEXTURE_2D, tableTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, Profile::kBins, 2 * Profile::kRows, 0, GL_RGBA, GL_FLOAT, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	saved.Restore();

	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

FFResult WooferPlugin::DeInitGL()
{
	uniformCache.clear();
	shader.FreeGLResources();
	if( vao != 0 )
		glDeleteVertexArrays( 1, &vao );
	if( tableTexture != 0 )
		glDeleteTextures( 1, &tableTexture );
	vao = tableTexture = 0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
void WooferPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;
	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime. Vote against the wall clock for a few frames, then stop asking.
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;
			}
		}
		if( hostDelta > 0.0 && clockScale == 0.0 && ++clockSamples >= kClockPatience )
		{
			clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;
			settledJump = true;
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;
	//Double throughout: Resolume's clock has been measured at ~499 million ms.
	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
}

void WooferPlugin::SetPerturbForTest( const Perturb& p, const ProfileSettings& flags )
{
	perturb      = p;
	profileFlags = flags;
	++perturbGeneration;
}

void WooferPlugin::ApplySettings()
{
	DriverSettings d;
	d.preset         = OptionIndex( params[ PT_DRIVER ], kPresetCount );
	d.qtc            = QtcFromParam( params[ PT_BOX ] );
	d.xmaxScale      = XmaxFromParam( params[ PT_XMAX ] );
	d.offsetFraction = OffsetFromParam( params[ PT_OFFSET ] );
	d.breakupScale   = BreakupFromParam( params[ PT_BREAKUP ] );

	ProfileSettings p   = profileFlags;
	p.size              = SizeFromParam( params[ PT_SIZE ] );
	p.camera            = CameraFromParam( params[ PT_CAMERA ] );
	p.ripple            = RippleFromParam( params[ PT_RIPPLE ] );

	const bool regen = appliedGeneration != perturbGeneration;
	if( regen )
	{
		model.perturb     = perturb;
		appliedGeneration = perturbGeneration;
	}
	//A control change is not a reset: the cone keeps moving.
	if( !configured || regen || !same( d, appliedDriver ) )
	{
		model.Configure( d );
		appliedDriver = d;
		profile.Configure( model, p );
		appliedProfile = p;
	}
	else if( !same( p, appliedProfile ) )
	{
		profile.Configure( model, p );
		appliedProfile = p;
	}
	configured = true;
}

void WooferPlugin::Lamp( int width, int height, float colour[ 4 ], float pos[ 2 ] )
{
	const double aspect = static_cast< double >( width ) / height;
	const double reach  = profile.RhoMax() + 0.035;
	const double c      = std::sqrt( 0.5 );
	pos[ 0 ]            = static_cast< float >( std::min( reach * c, 0.5 * aspect - 0.025 ) );
	pos[ 1 ]            = static_cast< float >( std::max( -reach * c, -0.5 + 0.025 ) );
	lampPixelX          = ( pos[ 0 ] + 0.5 * aspect ) * height;
	lampPixelY          = ( pos[ 1 ] + 0.5 ) * height;

	colour[ 3 ] = OptionIndex( params[ PT_LAMP ], 2 ) == 1 ? 1.0f : 0.0f;
	const earshot::CaptureState state = audio.State();
	auto set = [ & ]( float r, float g, float b ) {
		colour[ 0 ] = r;
		colour[ 1 ] = g;
		colour[ 2 ] = b;
	};
	const bool chosenDown = state == earshot::CaptureState::Missing || state == earshot::CaptureState::Failed;
	if( state == earshot::CaptureState::Silent )
		set( 1.0f, 0.05f, 0.02f );// red, steady: open and delivering zeros
	else if( chosenDown )
		set( std::fmod( now, 1.0 ) < 0.5 ? 1.0f : 0.15f, 0.03f, 0.02f );// red, blinking
	else if( audio.FromDevice() )
		set( 0.15f, 1.0f, 0.25f );// green: a device drives the cone
	else
		set( 1.0f, 0.62f, 0.05f );// amber: Resolume's FFT
	if( model.StopHits() > 0 && model.Count() - model.LastHit() < static_cast< uint64_t >( kHitFlash * DriverModel::kTapRate ) )
		set( 1.0f, 1.0f, 1.0f );// the coil hit its stop
}

//---------------------------------------------------------------------------
void WooferPlugin::Advance( double dt )
{
	ApplySettings();

	//The audio, as volts at 32 kHz.
	auto t0          = std::chrono::steady_clock::now();
	float bins[ 64 ] = {};
	int binCount     = 0;
	if( const ParamInfo* info = FindParamInfo( PT_AUDIO ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), 64 ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	const double drive = DriverModel::ReferenceVolts( model.Driver() ) * std::pow( 10.0, DriveFromParam( params[ PT_DRIVE ] ) / 20.0 );
	//Resolume's 64 bins are linear to half the host's audio engine rate, 375 Hz
	//each at 48 kHz, and earshot shares each among the bands it overlaps
	//(earshot 0.1.1). Until the host sends a rate, earshot's 48 kHz.
	audio.SetHostRate( hostRate.load( std::memory_order_relaxed ) );
	audio.Pull( now, dt, bins, binCount, static_cast< Channel >( OptionIndex( params[ PT_CHANNEL ], static_cast< int >( Channel::Count ) ) ),
	            static_cast< FftLaw >( OptionIndex( params[ PT_FFT_LAW ], static_cast< int >( FftLaw::Count ) ) ), drive, volts );
	audioMicros = microsSince( t0 );

	//Every sample, at the audio rate.
	t0 = std::chrono::steady_clock::now();
	model.Run( volts.data(), static_cast< int >( volts.size() ) );
	integrateMicros = microsSince( t0 );
}

void WooferPlugin::AdvanceForTest( double hostSeconds, double dt )
{
	now = hostSeconds;
	Advance( dt );
}

//---------------------------------------------------------------------------
FFResult WooferPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = pgl->inputTextures[ 0 ];
	if( !shader.IsReady() || tableTexture == 0 )
		return FF_FAIL;

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );
	const int width  = static_cast< int >( input->Width );
	const int height = static_cast< int >( input->Height );
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;

	//The driver lives in metres; a new raster changes only the drawing
	//(wftest --resize). The negative control resets it.
	if( resetOnResize && lastWidth > 0 && ( width != lastWidth || height != lastHeight ) )
		model.Reset();
	lastWidth  = width;
	lastHeight = height;

	Advance( dt );

	//The exposure: kRows instants over Exposure, ending at the newest sample.
	auto t0               = std::chrono::steady_clock::now();
	const double exposure = ExposureFromParam( params[ PT_EXPOSURE ] );
	rows                  = exposure > 0.0 ? Profile::kRows : 1;
	const size_t rowFloats = static_cast< size_t >( Profile::kBins ) * 4;
	for( int k = 0; k < rows; ++k )
	{
		const double back = exposure * DriverModel::kTapRate * ( k + 0.5 ) / rows;
		const Sample s    = model.At( rows == 1 ? 0.0 : back );
		profile.Row( model, s, &table[ rowFloats * static_cast< size_t >( k ) ],
		             &table[ rowFloats * static_cast< size_t >( Profile::kRows + k ) ] );
	}
	tableMicros = microsSince( t0 );

	glBindTexture( GL_TEXTURE_2D, tableTexture );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, Profile::kBins, rows, GL_RGBA, GL_FLOAT, table.data() );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, Profile::kRows, Profile::kBins, rows, GL_RGBA, GL_FLOAT,
	                 &table[ rowFloats * Profile::kRows ] );
	glBindTexture( GL_TEXTURE_2D, 0 );

	float lamp[ 4 ], lampPos[ 2 ];
	Lamp( width, height, lamp, lampPos );

	const double elevation = LightAngleFromParam( params[ PT_LIGHT_ANGLE ] );
	const double turn      = LightTurnFromParam( params[ PT_LIGHT_TURN ] );
	const float gloss      = std::clamp( params[ PT_GLOSS ], 0.0f, 1.0f );
	const FFGLTexCoords maxUV = GetMaxGLTexCoords( *input );

	glBindFramebuffer( GL_FRAMEBUFFER, pgl->HostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( shader.GetGLID() );
	bindUnit( 0, input->Handle );
	bindUnit( 1, tableTexture );
	glUniform1i( where( "InputTexture" ), 0 );
	glUniform1i( where( "Table" ), 1 );
	glUniform2f( where( "MaxUV" ), maxUV.s, maxUV.t );
	glUniform2i( where( "Size" ), width, height );
	glUniform2i( where( "ViewOrigin" ), hostViewport[ 0 ], hostViewport[ 1 ] );
	glUniform1f( where( "RhoMax" ), static_cast< float >( profile.RhoMax() ) );
	glUniform1i( where( "Bins" ), Profile::kBins );
	glUniform1i( where( "Rows" ), rows );
	glUniform1i( where( "RowStride" ), Profile::kRows );
	glUniform1f( where( "RayScale" ), static_cast< float >( profile.RayScale() ) );
	glUniform3f( where( "LightDir" ), static_cast< float >( std::cos( elevation ) * std::cos( turn ) ),
	             static_cast< float >( std::cos( elevation ) * std::sin( turn ) ), static_cast< float >( std::sin( elevation ) ) );
	glUniform1f( where( "Ambient" ), std::clamp( params[ PT_AMBIENT ], 0.0f, 1.0f ) );
	glUniform1f( where( "GlossAlpha" ), 0.5f - 0.4f * gloss );
	glUniform1f( where( "GlossAmount" ), gloss );
	glUniform1i( where( "ViewIndex" ), OptionIndex( params[ PT_VIEW ], static_cast< int >( View::Count ) ) );
	glUniform1f( where( "HeightScale" ), static_cast< float >( 1.0 / ( 2.2 * model.Xmech() ) ) );
	glUniform4fv( where( "LampColour" ), 1, lamp );
	glUniform2fv( where( "LampPos" ), 1, lampPos );
	glUniform1f( where( "LampRadius" ), 0.008f );
	glUniform1f( where( "MixAmount" ), std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
	glUniform1i( where( "Hooks" ), hooks );
	glBindVertexArray( vao );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
	unbindTextureUnits( 2 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult WooferPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void WooferPlugin::SetSampleRate( unsigned int rate )
{
	CFFGLPlugin::SetSampleRate( rate );
	hostRate.store( rate, std::memory_order_relaxed );
}

char* WooferPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult WooferPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult WooferPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;
	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	if( index == PT_RESCAN )
	{
		//On the press, once (spasis's Rescan): new elements, and the host told.
		const bool down = value >= 0.5f;
		if( down && !rescanHeld )
		{
			audio.Refresh();
			const auto& slots = audio.Slots();
			for( int i = 0; i < kInputSlots; ++i )
				SetParamElementInfo( PT_INPUT, static_cast< unsigned int >( i ), slots[ static_cast< size_t >( i ) ].label.c_str(),
				                     slots[ static_cast< size_t >( i ) ].value );
			RaiseParamEvent( PT_INPUT, FF_EVENT_FLAG_ELEMENTS );
			//A remembered device that was missing may be back: ask again.
			if( params[ PT_INPUT ] != 0.0f )
			{
				audio.Select( 0.0f );
				audio.Select( params[ PT_INPUT ] );
			}
		}
		rescanHeld       = down;
		params[ index ] = value;
		return FF_SUCCESS;
	}
	if( index == PT_INPUT )
		audio.Select( value );
	params[ index ] = value;
	return FF_SUCCESS;
}

float WooferPlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

} // namespace woofer
