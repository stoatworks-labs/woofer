//===========================================================================
// run.inc -- wftest's modes and main(). Included by main.cpp after its
// anonymous namespace.
//===========================================================================

namespace
{

enum class Source
{
	Track,   ///< the harness's track, both channels
	Left,    ///< the track on the left channel only (the Channel control's sweep)
	Silent,  ///< zeros, through feed()
	HostFft, ///< nothing fed: Resolume's FFT buffer only, a synthetic spectrum
	Wav      ///< --wav
};

/// The audio a render is fed.
void chooseAudio( Rig& rig, const Wav* wav, Source source )
{
	Feeder& feeder = rig.feeder;
	switch( source )
	{
	case Source::Wav:
		feeder.wav  = wav;
		feeder.rate = wav->rate;
		break;
	case Source::Silent: feeder.signal = mono( []( double ) { return 0.0; } ); break;
	case Source::Left:
		feeder.signal = []( double t, float& l, float& r ) {
			l = static_cast< float >( musicAt( t ) );
			r = 0.0f;
		};
		break;
	case Source::HostFft:
		//A kick's spectrum on the beat, decaying: low bins strong, a tail up
		//the spectrum, so both the bin law and the layout change the bands.
		rig.beforeFrame = [ &rig ]( int frame ) {
			const double t = std::fmod( frame / rig.fps, 0.5 );
			for( int e = 0; e < 64; ++e )
			{
				const double v = std::exp( -t / 0.12 ) * 0.8 * std::exp( -e / 6.0 ) + 0.05 * std::exp( -e / 30.0 );
				rig.plugin.SetParamElementValue( PT_AUDIO, static_cast< unsigned int >( e ), static_cast< float >( v ) );
			}
		};
		break;
	case Source::Track: feeder.signal = mono( musicAt ); break;
	}
}

bool applyAll( WooferPlugin& plugin, const std::vector< std::string >& settings )
{
	for( const std::string& setting : settings )
	{
		std::string error;
		if( !applySetting( plugin, setting, error ) )
		{
			std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
			return false;
		}
	}
	return true;
}

//===========================================================================
// --pipe and --film. Raw RGBA, top row first, on the synthetic clock. --pipe
// reads a frame from stdin for every frame it writes (the clip); --film N
// renders N frames of the harness's card. Audio: --wav, else the track.
//===========================================================================
int runPipe( int width, int height, double fps, const std::string& scriptPath, int count, bool readStdin, const std::vector< std::string >& settings,
             const Wav* wav, Source source )
{
	Rig rig;
	rig.fps = fps;
	if( !rig.Init( width, height ) )
		return 1;
	if( !applyAll( rig.plugin, settings ) )
		return 2;
	chooseAudio( rig, wav, source );
	std::map< unsigned int, Cue > automation;
	if( !scriptPath.empty() )
	{
		std::ifstream file( scriptPath );
		std::string error;
		if( !file )
			error = "cannot open " + scriptPath;
		else
			bindScript( rig.plugin, file, scriptPath, automation, error );
		if( !error.empty() )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
	}

	//What the cone did, on stderr when the run ends: a demo clip's run says
	//how far the music drove it, which no still can.
	int frames   = 0;
	auto summary = [ & ]() {
		const DriverModel& model = rig.plugin.ModelForTest();
		std::fprintf( stderr, "%d frames at %g fps; %s; max |x| %.3f mm of Xmech %.1f (%.0f%%); %llu stop hits; %llu tap samples\n", frames, fps,
		              model.Driver().model, model.MaxAbsX() * 1e3, model.Xmech() * 1e3, 100.0 * model.MaxAbsX() / model.Xmech(),
		              static_cast< unsigned long long >( model.StopHits() ), static_cast< unsigned long long >( model.Count() ) );
	};
	Bytes in( static_cast< size_t >( width ) * height * 4 );
	Floats picture( in.size() );
	for( int index = 0; readStdin || index < count; ++index )
	{
		if( readStdin )
		{
			size_t filled = 0;
			while( filled < in.size() )
			{
				const ssize_t got = read( STDIN_FILENO, in.data() + filled, in.size() - filled );
				if( got <= 0 )
					break;
				filled += static_cast< size_t >( got );
			}
			//A partial frame is the end of the stream, never a frame.
			if( filled < in.size() )
			{
				if( filled > 0 )
					std::fprintf( stderr, "partial frame at the end (%zu of %zu bytes): dropped\n", filled, in.size() );
				break;
			}
			for( int y = 0; y < height; ++y )
				for( int x = 0; x < width * 4; ++x )
					picture[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = in[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
			rig.Upload( picture );
		}
		//A track does nothing before its first key (honeydew's trap).
		for( const auto& cue : automation )
			if( index >= cue.second.track.front().first || cue.second.ramp )
				rig.plugin.SetFloatParameter( cue.first, valueAt( cue.second.track, index, cue.second.ramp ) );
		if( !rig.Render( 1 ) )
			return 1;

		const Floats out = rig.Output();
		Bytes bytes( in.size() );
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width * 4; ++x )
				bytes[ static_cast< size_t >( y ) * width * 4 + x ] = static_cast< unsigned char >(
					std::lround( std::clamp( out[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ], 0.0f, 1.0f ) * 255.0f ) );
		size_t written = 0;
		while( written < bytes.size() )
		{
			const ssize_t put = write( STDOUT_FILENO, bytes.data() + written, bytes.size() - written );
			//The reader has gone. SIGPIPE is ignored in main(), so this is
			//EPIPE and not a silent 141: say so and stop.
			if( put <= 0 )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				summary();
				return 1;
			}
			written += static_cast< size_t >( put );
		}
		frames = index + 1;
	}
	summary();
	return 0;
}

} // namespace

//---------------------------------------------------------------------------
int main( int argc, char** argv )
{
	//Before any plugin exists: every earshot the harness makes lists and opens
	//the scripted devices, never the OS's.
	AudioInput::SetBackendFactoryForTest( scriptedBackend );
	//Line-buffered even into a pipe or a file: a run that dies part way keeps
	//every line it printed (a whole --negative run once vanished into an
	//unflushed buffer). Frames go out through write(2), never stdio.
	std::setvbuf( stdout, nullptr, _IOLBF, 0 );

	std::string outPath = "/tmp/woofer.png";
	std::vector< std::string > settings;
	int width = 1280, height = 720, frames = 180;
	double fps = 60.0;
	std::string mode, scriptPath, wavPath;
	int filmFrames = -1;
	bool sizeGiven = false;
	Source source  = Source::Track;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			std::printf( "wftest -- render Woofer offline and measure the cone\n\n"
			             "  --out PATH        render and write a PNG (default /tmp/woofer.png)\n"
			             "  --size WxH        render size (default 1280x720)\n"
			             "  --frames N        frames before reading back (default 180)\n"
			             "  --fps N           the synthetic clock's rate (default 60)\n"
			             "  --wav PATH        the audio, fed through earshot's feed() as if from a device (default: the harness's track)\n"
			             "  --silent          feed silence instead\n"
			             "  --left-only       the track on the left channel only\n"
			             "  --host-fft        feed nothing: Resolume's FFT buffer only, a synthetic kick spectrum (the fallback)\n"
			             "  --set \"Name=V\"    set a parameter by its display name (an option by its name). Repeatable.\n"
			             "  --list            every parameter and its default\n"
			             "  --pipe            raw RGBA frames in on stdin (the clip, at --size), out on stdout\n"
			             "  --film N          N frames of the harness's card, raw RGBA on stdout\n"
			             "  --script PATH     cues for --pipe/--film: 'frame Name value'\n\n"
			             "  --all             every check below, at 1280x720 and 320x180 (--size: that raster alone)\n"
			             "  --negative        each check against the wrong models it must catch\n"
			             "  --bench           ms a frame at 720p and 1080p, and the integration's CPU cost\n"
			             "  --bench4k         the same, and 4K once\n"
			             "  --offline         the checks and negative controls that need no GL context (CI)\n\n"
			             "  No step opens or lists a real audio device: a scripted earshot backend stands in.\n\n"
			             "  checks, one flag each:" );
			for( const CheckEntry& check : checks() )
				std::printf( " --%s", check.flag );
			std::printf( "\n" );
			return 0;
		}
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--frames" && hasNext )
			frames = std::atoi( argv[ ++i ] );
		else if( argument == "--fps" && hasNext )
			fps = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--wav" && hasNext )
			wavPath = argv[ ++i ];
		else if( argument == "--silent" )
			source = Source::Silent;
		else if( argument == "--left-only" )
			source = Source::Left;
		else if( argument == "--host-fft" )
			source = Source::HostFft;
		else if( argument == "--pipe" )
			mode = "pipe";
		else if( argument == "--film" && hasNext )
		{
			mode       = "film";
			filmFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
		}
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--list" )
			mode = "list";
		else if( argument == "--size" && hasNext )
		{
			const std::string value = argv[ ++i ];
			const size_t cross      = value.find( 'x' );
			if( cross != std::string::npos )
			{
				width  = std::atoi( value.substr( 0, cross ).c_str() );
				height = std::atoi( value.substr( cross + 1 ).c_str() );
			}
			else
				width = height = 0;
			sizeGiven = true;
		}
		else if( argument.rfind( "--", 0 ) == 0 )
			mode = argument.substr( 2 );
		else
		{
			std::fprintf( stderr, "unknown argument '%s' (try --help)\n", argument.c_str() );
			return 2;
		}
	}
	if( width <= 0 || height <= 0 || !( fps > 0.0 ) )
	{
		std::fprintf( stderr, "--size and --fps must be positive\n" );
		return 2;
	}
	if( sizeGiven )
		kRasters = { { width, height } };

	Wav wav;
	if( !wavPath.empty() )
	{
		std::string error;
		if( !readWav( wavPath, wav, error ) )
		{
			std::fprintf( stderr, "--wav: %s\n", error.c_str() );
			return 2;
		}
	}
	const Wav* wavIn = wavPath.empty() ? nullptr : &wav;
	if( wavIn != nullptr )
		source = Source::Wav;
	//WFTEST_FIRST_FAILURE=1: a check may stop at its first failure (mutate.sh).
	if( std::getenv( "WFTEST_FIRST_FAILURE" ) != nullptr )
		g_negative = true;

	if( mode == "list" )
	{
		WooferPlugin plugin;
		std::printf( "%-3s %-18s %-9s %s\n", "id", "name", "kind", "default" );
		for( const NamedParameter& parameter : listParameters( plugin ) )
		{
			std::printf( "%-3u %-18s %-9s %.4f", parameter.index, parameter.name.c_str(), kindName( parameter.type ), parameter.value );
			if( parameter.type == FF_TYPE_OPTION )
			{
				std::printf( "  [" );
				for( unsigned int e = 0; e < plugin.GetNumParamElements( parameter.index ); ++e )
					std::printf( "%s%s", e ? " | " : "", plugin.GetParamElementName( parameter.index, e ) );
				std::printf( "]" );
			}
			std::printf( "\n" );
		}
		return 0;
	}

	//A reader that hangs up must end --pipe/--film with exit 1 and a message,
	//not SIGPIPE's silent 141: ignored here, the write fails with EPIPE.
	std::signal( SIGPIPE, SIG_IGN );

	if( mode == "offline" )
		return runOffline();
	for( const CheckEntry& check : checks() )
		if( mode == check.flag && check.offline && std::getenv( "WFTEST_NO_GL" ) != nullptr )
			return check.run( HarnessPerturb {} );

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL 4.1 core context\n" );
		return 1;
	}
	g_haveGL = true;

	int result = 0;
	bool ran   = false;
	for( const CheckEntry& check : checks() )
		if( mode == check.flag )
		{
			result = check.run( HarnessPerturb {} );
			ran    = true;
		}

	if( ran )
		result = Verdict();
	else if( mode == "all" )
	{
		std::vector< std::string > failed;
		for( const CheckEntry& check : checks() )
		{
			const int before = g_failures;
			check.run( HarnessPerturb {} );
			if( g_failures != before )
				failed.push_back( check.flag );
		}
		std::printf( "\n%zu checks, %d measurements, %zu failed", checks().size(), g_checks, failed.size() );
		for( const std::string& name : failed )
			std::printf( " %s", name.c_str() );
		std::printf( "\n" );
		result = failed.empty() ? 0 : 1;
		Verdict();
	}
	else if( mode == "pipe" )
		result = runPipe( width, height, fps, scriptPath, 0, true, settings, wavIn, source );
	else if( mode == "film" )
		result = runPipe( width, height, fps, scriptPath, filmFrames, false, settings, wavIn, source );
	else if( mode == "negative" )
		result = runNegative();
	else if( mode == "bench" || mode == "bench4k" )
		result = runBench( mode == "bench4k" );
	else if( !mode.empty() )
	{
		std::fprintf( stderr, "unknown mode --%s (try --help)\n", mode.c_str() );
		result = 2;
	}
	else
	{
		Rig rig;
		rig.fps = fps;
		if( !rig.Init( width, height ) )
			result = 1;
		else if( !applyAll( rig.plugin, settings ) )
			result = 2;
		else
		{
			chooseAudio( rig, wavIn, source );
			if( !rig.Render( std::max( frames, 1 ) ) )
				result = 1;
			else if( writePng( outPath, width, height, rig.Output() ) )
			{
				const DriverModel& model = rig.plugin.ModelForTest();
				std::printf( "wrote %s -- %dx%d, %d frames at %g fps (%.2f s); %s; x now %+.3f mm, max |x| %.3f mm of Xmech %.1f; %llu stop hits; "
				             "%llu tap samples\n",
				             outPath.c_str(), width, height, frames, fps, frames / fps, model.Driver().model, model.Newest().x * 1e3,
				             model.MaxAbsX() * 1e3, model.Xmech() * 1e3, static_cast< unsigned long long >( model.StopHits() ),
				             static_cast< unsigned long long >( model.Count() ) );
			}
			else
				result = 1;
		}
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return result;
}
