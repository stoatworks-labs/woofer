//===========================================================================
// checks.inc -- wftest's checks. Included by main.cpp inside its anonymous
// namespace, after the rig. SPEC-woofer.md, "Harness claims", is the table
// these implement; each check prints the tolerance and where it came from.
//===========================================================================

bool g_haveGL   = false;///< a context exists: the picture halves run
bool g_negative = false;///< inside --negative: a long check may stop at its first failure

bool stopEarly()
{
	return g_negative && g_failures > 0;
}

//---------------------------------------------------------------------------
// The driver as the HARNESS computes it: from the preset table and the
// control's own mapping, never from the model's internals.
//---------------------------------------------------------------------------
struct Truth
{
	Preset p;
	double fs = 0, qts = 0, vas = 0;
	double qtc = 0, alpha = 0, vb = 0, kt = 0;
	double xlin = 0, plateau = 0, xmech = 0;
	double vref = 0;///< Drive's 0 dB: Xlin at DC in a Qtc 0.707 box
	double fc   = 0;
};

Truth truthOf( int preset, double qtc )
{
	Truth t;
	t.p             = PresetAt( preset );
	const Preset& p = t.p;
	const double ws = 1.0 / std::sqrt( p.Mms * p.Cms );
	t.fs            = ws / ( 2.0 * kPi );
	const double qes = ws * p.Mms * p.Re / ( p.Bl * p.Bl );
	const double qms = ws * p.Mms / p.Rms;
	t.qts            = qes * qms / ( qes + qms );
	t.vas            = kGamma * kP0 * p.Sd * p.Sd * p.Cms;
	//What the Box control actually sets: through its float parameter.
	t.qtc   = qtc > 0.0 ? QtcFromParam( ParamFromQtc( qtc ) ) : QtcFromParam( 0.0f );
	t.alpha = t.qtc > t.qts ? ( t.qtc / t.qts ) * ( t.qtc / t.qts ) - 1.0 : 0.0;
	t.vb    = t.alpha > 0.0 ? t.vas / t.alpha : 0.0;
	t.kt    = ( 1.0 + t.alpha ) / p.Cms;
	t.xlin  = 0.5 * ( p.Hvc - p.Hg );
	//The gap field is flat over |z| <= Hg/4 and zero past 3 Hg/4: an
	//overhung coil's overlap is constant while that support is inside it.
	t.plateau        = 0.5 * p.Hvc - 0.75 * p.Hg;
	t.xmech          = p.Xmech;
	const double a07 = std::max( 0.0, ( 0.707 / t.qts ) * ( 0.707 / t.qts ) - 1.0 );
	t.vref           = t.xlin * p.Re * ( 1.0 + a07 ) / ( p.Bl * p.Cms );
	t.fc             = t.fs * std::sqrt( 1.0 + t.alpha );
	return t;
}

Complex closedFormOf( const Truth& t, double hz, bool withLe = true )
{
	return closedForm( t.p, t.alpha, hz, withLe );
}

/// Force to displacement, with the amplifier a short (V = 0) and Le.
Complex forceCompliance( const Truth& t, double hz )
{
	const Complex s( 0.0, 2.0 * kPi * hz );
	const Preset& p = t.p;
	return 1.0 / ( p.Mms * s * s + p.Rms * s + t.kt + p.Bl * p.Bl * s / ( p.Re + s * p.Le ) );
}

/// The adiabatic air spring's second-order coefficient: Fbox ~ -Kbox x + k2 x^2.
double airSpringK2( const Truth& t )
{
	if( t.vb <= 0.0 )
		return 0.0;
	return kGamma * ( kGamma + 1.0 ) * kP0 * t.p.Sd * t.p.Sd * t.p.Sd / ( 2.0 * t.vb * t.vb );
}

//---------------------------------------------------------------------------
// One configuration of the controls a physics check depends on (honeydew's
// lesson: set every one).
//---------------------------------------------------------------------------
struct DriveSetup
{
	int preset     = kDefaultPreset;
	double qtc     = 0.0;///< <= 0: the infinite baffle (Box at 0.3, under every preset's Qts)
	double offset  = 0.0;///< x Hg
	double driveDb = 0.0;
};

void setup( Physics& ph, const DriveSetup& s, const HarnessPerturb& hp )
{
	ph.Apply( hp );
	ph.Set( PT_DRIVER, static_cast< float >( s.preset ) );
	ph.Set( PT_BOX, s.qtc > 0.0 ? ParamFromQtc( s.qtc ) : 0.0f );
	ph.Set( PT_XMAX, ParamFromXmax( 1.0 ) );
	ph.Set( PT_OFFSET, ParamFromOffset( s.offset ) );
	ph.Set( PT_BREAKUP, ParamFromBreakup( 1.0 ) );
	ph.Set( PT_DRIVE, ParamFromDrive( s.driveDb ) );
	ph.Set( PT_CHANNEL, 0.0f );
	ph.Set( PT_INPUT, 0.0f );
	ph.Prime();
}

/// The plugin's own volts per feed unit at 0 dB Drive.
double voltsPerUnit( int preset )
{
	return DriverModel::ReferenceVolts( PresetAt( preset ) );
}

/// A DFT bin of v[from .. from+n) at hz, where sample j is at time (j + shift)/rate.
Complex dftAt( const Doubles& v, size_t from, size_t n, double hz, double rate, double shift )
{
	Complex sum = 0.0;
	for( size_t k = 0; k < n; ++k )
	{
		const double a = -2.0 * kPi * hz * ( static_cast< double >( from + k ) + shift ) / rate;
		sum += v[ from + k ] * Complex( std::cos( a ), std::sin( a ) );
	}
	return sum * ( 2.0 / static_cast< double >( n ) );
}

/// A steady sine's last second, measured. x[k] is the state at volts[k-1]'s
/// instant (the integrator runs one sample behind), so V is read one sample
/// earlier with the same time base. An integer frequency over exactly
/// kTapRate samples makes every harmonic and every alias of the tap
/// orthogonal: the bins are exact.
struct Steady
{
	Complex X[ 6 ];///< harmonics 1..5 of x; X[0] is the mean (real)
	Complex V1;
	double peak   = 0;///< max |x - mean| over the window
	double rms    = 0;///< of x - mean
	double maxAbs = 0;///< over every substep since the start
	uint64_t hits = 0;
	bool finite   = true;
	size_t samples = 0;
};

Steady measureSteady( Physics& ph, double hz )
{
	Steady r;
	const size_t n = DriverModel::kTapRate;
	const size_t N = std::min( ph.x.size(), ph.volts.size() );
	r.samples      = N;
	if( N < n + 2 )
	{
		r.finite = false;
		return r;
	}
	const size_t from = N - n;
	double mean       = 0.0;
	for( size_t k = from; k < N; ++k )
	{
		mean += ph.x[ k ];
		if( !std::isfinite( ph.x[ k ] ) )
			r.finite = false;
	}
	mean /= static_cast< double >( n );
	r.X[ 0 ] = mean;
	for( int h = 1; h <= 5; ++h )
		r.X[ h ] = dftAt( ph.x, from, n, h * hz, DriverModel::kTapRate, -1.0 );
	r.V1         = dftAt( ph.volts, from - 1, n, hz, DriverModel::kTapRate, 0.0 );
	double sumSq = 0.0;
	for( size_t k = from; k < N; ++k )
	{
		const double d = ph.x[ k ] - mean;
		r.peak         = std::max( r.peak, std::fabs( d ) );
		sumSq += d * d;
	}
	r.rms    = std::sqrt( sumSq / static_cast< double >( n ) );
	r.maxAbs = ph.Model().MaxAbsX();
	r.hits   = ph.Model().StopHits();
	return r;
}

/// A sine faded in over 1 s, settled, then measured over a whole second.
Steady runSteady( const DriveSetup& s, const HarnessPerturb& hp, double hz, double amplitude, double settle = 2.5 )
{
	Physics ph;
	setup( ph, s, hp );
	ph.feeder.signal = mono( [ = ]( double t ) { return amplitude * fadeIn( t, 1.0 ) * std::sin( 2.0 * kPi * hz * t ); } );
	ph.RunFor( 1.0 + settle + 1.1 );
	return measureSteady( ph, hz );
}

/// Feed units for a linear-prediction peak of `peak` metres at hz.
double amplitudeFor( const Truth& t, double hz, double peak, int preset )
{
	return peak / ( std::abs( closedFormOf( t, hz ) ) * voltsPerUnit( preset ) );
}

const char* boxName( double qtc )
{
	static char buffer[ 64 ];
	if( qtc <= 0.0 )
		return "infinite baffle";
	std::snprintf( buffer, sizeof( buffer ), "Qtc %.3g box", qtc );
	return buffer;
}

//===========================================================================
// --smallsignal
//===========================================================================
int runSmallSignal( const HarnessPerturb& hp )
{
	std::printf( "\n=== smallsignal: X/V against the sealed box's closed form with Le, 24 third-octave sines 10 Hz - 2 kHz\n"
	             "    (1e-3: twice Catmull-Rom's midpoint error for 2 kHz at 32 kHz, 4.9e-4; RK4 < 1e-5; the air spring's next order < 1e-4)\n" );
	const int before = g_failures;
	for( int preset = 0; preset < kPresetCount && !stopEarly(); ++preset )
		for( double qtc : { 0.707, 0.0 } )
		{
			const Truth t = truthOf( preset, qtc );
			double worst = 0.0, worstHz = 0.0, worstStroke = 0.0;
			bool allFinite = true;
			for( int k = 0; k < 24 && !stopEarly(); ++k )
			{
				const double hz   = std::round( 10.0 * std::pow( 2.0, k / 3.0 ) );
				const Complex H   = closedFormOf( t, hz );
				const Steady r    = runSteady( { preset, qtc, 0.0, 0.0 }, hp, hz, amplitudeFor( t, hz, 0.3 * t.plateau, preset ) );
				const Complex got = r.X[ 1 ] / r.V1;
				double err        = std::abs( got - H ) / std::abs( H );
				if( !std::isfinite( err ) || !r.finite )
				{
					err       = HUGE_VAL;
					allFinite = false;
				}
				if( err > worst )
				{
					worst   = err;
					worstHz = hz;
				}
				worstStroke = std::max( worstStroke, r.maxAbs / t.plateau );
			}
			Check( worst <= 1e-3, fmt( "%s, %s: worst |X/V - closed form| / |closed form| = %.2e at %g Hz (<= 1e-3)", t.p.model, boxName( qtc ),
			                           worst, worstHz ) );
			Check( allFinite && worstStroke <= 1.0, fmt( "%s, %s: the stroke stayed inside the Bl plateau (max |x| = %.3f of it)", t.p.model,
			                                             boxName( qtc ), worstStroke ) );
		}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --harmonics
//===========================================================================
int runHarmonics( const HarnessPerturb& hp )
{
	std::printf( "\n=== harmonics: (a) exactly linear inside the plateau; (b) the air spring's H2 from second-order perturbation\n"
	             "    (a: -120 dB, against rounding near -300 and a residual transient under -200; b: 2 %%, over twice the next order, O( Sd X / Vb ) <= 0.8 %%)\n" );
	const int before  = g_failures;
	const double hz   = 40.0;
	for( int preset = 0; preset < kPresetCount && !stopEarly(); ++preset )
	{
		const Truth t = truthOf( preset, 0.0 );
		const Steady r = runSteady( { preset, 0.0, 0.0, 0.0 }, hp, hz, amplitudeFor( t, hz, 0.5 * t.plateau, preset ) );
		double worst   = -HUGE_VAL;
		int worstH     = 0;
		for( int h = 2; h <= 5; ++h )
		{
			const double db = 20.0 * std::log10( std::abs( r.X[ h ] ) / std::abs( r.X[ 1 ] ) );
			if( !( db <= worst ) )
			{
				worst  = std::isfinite( db ) ? db : ( db < 0 ? -400.0 : 400.0 );
				worstH = h;
			}
		}
		if( !r.finite )
			worst = 400.0;
		Check( worst <= -120.0, fmt( "(a) %s, infinite baffle, %g Hz at half the plateau: loudest of H2-H5 is H%d at %.1f dB re H1 (<= -120)",
		                             t.p.model, hz, worstH, worst ) );
	}
	for( int preset = 0; preset < kPresetCount && !stopEarly(); ++preset )
	{
		const Truth t       = truthOf( preset, 0.707 );
		const Steady r      = runSteady( { preset, 0.707, 0.0, 0.0 }, hp, hz, amplitudeFor( t, hz, 0.8 * t.plateau, preset ) );
		const double x1     = std::abs( r.X[ 1 ] );
		const double got    = std::abs( r.X[ 2 ] ) / x1;
		const double want   = airSpringK2( t ) * x1 * std::abs( forceCompliance( t, 2.0 * hz ) ) / 2.0;
		const double eps    = t.p.Sd * x1 / t.vb;
		const double ratio  = got / want;
		Check( std::isfinite( ratio ) && std::fabs( ratio - 1.0 ) <= 0.02,
		       fmt( "(b) %s, Qtc 0.707 (Vb %.1f L), %g Hz, Sd X/Vb = %.4f: H2/H1 = %.3e, predicted %.3e (ratio %.5f, within 2 %%)", t.p.model,
		            t.vb * 1e3, hz, eps, got, want, ratio ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --xmech
//===========================================================================
/// Kicks every half second at full scale.
double kicksAt( double t )
{
	return kickAt( std::fmod( t, 0.5 ) ) / kickPeak();
}

int runXmech( const HarnessPerturb& hp )
{
	std::printf( "\n=== xmech: the coil never passes Xmech, with the stop engaged (Drive +30 dB)\n"
	             "    (exact in double: the stop writes +-Xmech; the picture within one 8-bit code)\n" );
	const int before = g_failures;
	for( int preset = 0; preset < kPresetCount && !stopEarly(); ++preset )
	{
		const Truth t = truthOf( preset, 0.707 );
		struct Drive
		{
			const char* name;
			std::function< double( double ) > f;
		};
		const Drive drives[] = { { "25 Hz sine", []( double tt ) { return std::sin( 2.0 * kPi * 25.0 * tt ); } }, { "kicks", kicksAt } };
		for( const Drive& d : drives )
		{
			Physics ph;
			setup( ph, { preset, 0.707, 0.1, 30.0 }, hp );
			ph.feeder.signal = mono( d.f );
			ph.RunFor( 2.0 );
			double traced = 0.0;
			for( double v : ph.x )
				traced = std::isfinite( v ) ? std::max( traced, std::fabs( v ) ) : HUGE_VAL;
			const double worst = std::max( traced, ph.Model().MaxAbsX() );
			Check( worst <= t.xmech && ph.Model().StopHits() > 0,
			       fmt( "%s, %s: max |x| over every substep %.6f mm, Xmech %.3f mm; %llu stop hits", t.p.model, d.name, worst * 1e3, t.xmech * 1e3,
			            static_cast< unsigned long long >( ph.Model().StopHits() ) ) );
		}
	}
	if( !g_haveGL || stopEarly() )
		return g_failures == before ? 0 : 1;

	//The picture: the Height view's dust cap never codes past Xmech (Ripple 0,
	//so the code is the stroke and not the breakup on top of it).
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !rig.Init( raster.w, raster.h ) )
			return 1;
		rig.Apply( hp );
		rig.Set( PT_DRIVER, static_cast< float >( kDefaultPreset ) );
		rig.Set( PT_DRIVE, ParamFromDrive( 30.0 ) );
		rig.Set( PT_VIEW, static_cast< float >( View::Height ) );
		rig.Set( PT_RIPPLE, 0.0f );
		rig.Set( PT_EXPOSURE, 0.0f );
		rig.feeder.signal = mono( []( double tt ) { return std::sin( 2.0 * kPi * 25.0 * tt ); } );
		double highest = 0.0, lowest = 255.0;
		for( int f = 0; f < 120; ++f )
		{
			if( !rig.Render( 1 ) )
				return 1;
			const Floats block = rig.Region( raster.w / 2 - 1, raster.h / 2 - 1, 2, 2 );
			for( size_t i = 0; i < block.size(); i += 4 )
			{
				highest = std::max( highest, block[ i ] * 255.0 );
				lowest  = std::min( lowest, block[ i ] * 255.0 );
			}
		}
		const double limit = ( 0.5 + 1.0 / 2.2 ) * 255.0;
		Check( highest <= limit + 1.0 && lowest >= 255.0 - limit - 1.0,
		       fmt( "%dx%d Height view: the dust cap coded %.2f .. %.2f; Xmech codes %.2f .. %.2f (one code of slack)", raster.w, raster.h, lowest,
		            highest, 255.0 - limit, limit ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --framerate
//===========================================================================
int runFramerate( const HarnessPerturb& hp )
{
	std::printf( "\n=== framerate: the same audio at 24, 30, 60 and 144 fps gives the same trace, bit for bit\n"
	             "    (exact: the integrator sees the same samples whatever the chunking; the frame compared within one context)\n" );
	const int before = g_failures;
	const double rates[] = { 60.0, 24.0, 30.0, 144.0 };
	std::vector< std::vector< Sample > > runs;
	for( double fps : rates )
	{
		Physics ph;
		ph.fps        = fps;
		ph.keepStates = true;
		setup( ph, { kDefaultPreset, 0.707, 0.1, 20.0 }, hp );
		ph.feeder.signal = mono( musicAt );
		ph.RunFor( 2.0 );
		runs.push_back( ph.states );
	}
	for( size_t r = 1; r < runs.size(); ++r )
	{
		const size_t n    = std::min( runs[ 0 ].size(), runs[ r ].size() );
		size_t mismatches = 0, first = n;
		for( size_t k = 0; k < n; ++k )
		{
			const Sample& a = runs[ 0 ][ k ];
			const Sample& b = runs[ r ][ k ];
			bool same       = a.x == b.x && a.v == b.v && a.i == b.i;
			for( int m = 0; m < 4; ++m )
				same = same && a.q[ m ] == b.q[ m ];
			if( !same )
			{
				if( first == n )
					first = k;
				++mismatches;
			}
		}
		Check( mismatches == 0 && n >= 60000, fmt( "%g fps against 60: %zu of %zu tap samples differ (x, v, i, q)%s", rates[ r ], mismatches, n,
		                                         mismatches ? fmt( ", the first at %zu", first ).c_str() : "" ) );
	}
	if( !g_haveGL || stopEarly() )
		return g_failures == before ? 0 : 1;

	for( const Raster& raster : kRasters )
	{
		Floats frames[ 2 ];
		const double fpsOf[ 2 ] = { 24.0, 60.0 };
		for( int i = 0; i < 2; ++i )
		{
			Rig rig;
			rig.fps = fpsOf[ i ];
			if( !rig.Init( raster.w, raster.h ) )
				return 1;
			rig.Apply( hp );
			rig.Set( PT_DRIVE, ParamFromDrive( 20.0 ) );
			rig.feeder.signal = mono( musicAt );
			//Frame k carries audio to (k + 1) / fps: the last one to 1.000 s.
			if( !rig.Render( static_cast< int >( fpsOf[ i ] ) ) )
				return 1;
			frames[ i ] = rig.Output();
		}
		double worst = 0.0;
		for( size_t k = 0; k < frames[ 0 ].size(); ++k )
			worst = std::max( worst, static_cast< double >( std::fabs( frames[ 0 ][ k ] - frames[ 1 ][ k ] ) ) );
		Check( worst == 0.0, fmt( "%dx%d: the Lit frame at audio time 1.000 s, 24 fps against 60: largest difference %.3g codes (0)", raster.w,
		                          raster.h, worst * 255.0 ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --pumping
//===========================================================================
double centreCode( Rig& rig )
{
	const Floats block = rig.Region( rig.width / 2 - 1, rig.height / 2 - 1, 2, 2 );
	double sum         = 0.0;
	for( size_t i = 0; i < block.size(); i += 4 )
		sum += block[ i ];
	return sum / 4.0 * 255.0;
}

int runPumping( const HarnessPerturb& hp )
{
	std::printf( "\n=== pumping: a kick moves the cone, a hi-hat at the same peak level does not\n"
	             "    (1000x: above fc excursion goes as 1/f^2, so (7000/150)^2 ~ 2200 even were the whole kick at its top; the high-pass leaks -168 dB at 150 Hz)\n" );
	const int before = g_failures;
	for( int preset = 0; preset < kPresetCount && !stopEarly(); ++preset )
	{
		double peaks[ 2 ] = {};
		for( int which = 0; which < 2; ++which )
		{
			Physics ph;
			setup( ph, { preset, 0.707, 0.1, 6.0 }, hp );
			if( which == 0 )
				ph.feeder.signal = mono( []( double t ) { return kickAt( t - 0.1 ) / kickPeak(); } );
			else
				ph.feeder.signal = mono( []( double t ) { return hatAt( t - 0.1 ); } );
			ph.RunFor( 1.0 );
			for( double v : ph.x )
				peaks[ which ] = std::isfinite( v ) ? std::max( peaks[ which ], std::fabs( v ) ) : HUGE_VAL;
		}
		const Truth t = truthOf( preset, 0.707 );
		//Both finite: a blown-up model reads HUGE_VAL, and inf >= 100 x inf is true.
		Check( std::isfinite( peaks[ 0 ] ) && std::isfinite( peaks[ 1 ] ) && peaks[ 0 ] >= 1000.0 * peaks[ 1 ], fmt( "%s at +6 dB: kick peak |x| %.4f mm (%.1f %% of Xmech), hat %.2e mm: ratio %.0f (>= 1000)",
		                                              t.p.model, peaks[ 0 ] * 1e3, 100.0 * peaks[ 0 ] / t.xmech, peaks[ 1 ] * 1e3,
		                                              peaks[ 0 ] / peaks[ 1 ] ) );
	}
	if( !g_haveGL || stopEarly() )
		return g_failures == before ? 0 : 1;

	for( const Raster& raster : kRasters )
	{
		double moved[ 2 ] = {};
		for( int which = 0; which < 2; ++which )
		{
			Rig rig;
			if( !rig.Init( raster.w, raster.h ) )
				return 1;
			rig.Apply( hp );
			rig.Set( PT_VIEW, static_cast< float >( View::Height ) );
			if( which == 0 )
				rig.feeder.signal = mono( []( double t ) { return kickAt( t - 0.1 ) / kickPeak(); } );
			else
				rig.feeder.signal = mono( []( double t ) { return hatAt( t - 0.1 ); } );
			if( !rig.Render( 1 ) )
				return 1;
			const double rest = 0.5 * 255.0;
			for( int f = 1; f < 60; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				moved[ which ] = std::max( moved[ which ], std::fabs( centreCode( rig ) - rest ) );
			}
		}
		Check( moved[ 0 ] >= 8.0 && moved[ 1 ] <= 1.0,
		       fmt( "%dx%d Height view, the defaults: the kick moves the dust cap %.2f codes (>= 8), the hat %.3f (<= 1)", raster.w, raster.h,
		            moved[ 0 ], moved[ 1 ] ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --flattop
//===========================================================================
int runFlattop( const HarnessPerturb& hp )
{
	std::printf( "\n=== flattop: stiffness-controlled (Qtc 0.707 box, fc/4) and driven past Xlin, off the stop: the peak falls short and the wave flattens\n"
	             "    (Bl is <= 93.75 %% of rest past Xlin; a sine's crest is sqrt 2; 1e-3 is the small-signal bound)\n" );
	const int before = g_failures;
	for( int preset = 0; preset < kPresetCount && !stopEarly(); ++preset )
	{
		const Truth t    = truthOf( preset, 0.707 );
		const double hz  = std::max( 5.0, std::round( t.fc / 4.0 ) );
		const Steady big = runSteady( { preset, 0.707, 0.0, 0.0 }, hp, hz, amplitudeFor( t, hz, 1.8 * t.xlin, preset ) );
		const double predicted = std::abs( closedFormOf( t, hz ) ) * std::abs( big.V1 );
		const double crest     = big.peak / big.rms;
		Check( big.finite && big.hits == 0 && big.maxAbs < t.xmech && big.peak < ( 1.0 - 0.0625 ) * predicted && crest < std::sqrt( 2.0 ) - 0.01,
		       fmt( "%s, %g Hz (fc/4), linear prediction %.3f mm = %.2f Xlin: peak %.3f mm (%.1f %% short, > 6.25), crest %.4f (< %.4f); "
		            "max |x| %.3f of Xmech, %llu stop hits",
		            t.p.model, hz, predicted * 1e3, predicted / t.xlin, big.peak * 1e3, 100.0 * ( 1.0 - big.peak / predicted ), crest,
		            std::sqrt( 2.0 ) - 0.01, big.maxAbs / t.xmech, static_cast< unsigned long long >( big.hits ) ) );
		const Steady small = runSteady( { preset, 0.707, 0.0, 0.0 }, hp, hz, amplitudeFor( t, hz, 0.1 * t.plateau, preset ) );
		const double c2    = small.peak / small.rms;
		Check( std::fabs( c2 - std::sqrt( 2.0 ) ) <= 1e-3, fmt( "%s, %g Hz at a tenth of the plateau: crest %.6f (sqrt 2 within 1e-3)", t.p.model, hz, c2 ) );
		if( !g_quiet )
		{
			//Reported, not claimed: nearer resonance the same drive SHARPENS the
			//wave, because Bl's loss also takes the electrical damping away.
			const double near2 = std::max( 5.0, std::round( t.fc / 2.0 ) );
			const Steady r     = runSteady( { preset, 0.707, 0.0, 0.0 }, hp, near2, amplitudeFor( t, near2, 1.8 * t.xlin, preset ) );
			Note( fmt( "at %g Hz (fc/2) the same drive gives crest %.4f: damping-controlled, the ends run out faster as Bl falls", near2, r.peak / r.rms ) );
		}
	}
	//Reported, not claimed: how long a stroke driven through Xmech sits on
	//the stop. Below fc it is pressed there; nearer resonance it bounces off
	//(restitution 0.3), so "the stop flat-tops it" is not true everywhere.
	for( int preset = 0; preset < kPresetCount && !g_quiet; ++preset )
	{
		const Truth t = truthOf( preset, 0.707 );
		Physics ph;
		setup( ph, { preset, 0.707, 0.0, 30.0 }, hp );
		ph.feeder.signal = mono( []( double tt ) { return std::sin( 2.0 * kPi * 25.0 * tt ); } );
		ph.RunFor( 2.0 );
		size_t held = 0, counted = 0;
		for( size_t k = ph.x.size() / 2; k < ph.x.size(); ++k, ++counted )
			held += std::fabs( ph.x[ k ] ) >= t.xmech * ( 1.0 - 1e-9 ) ? 1 : 0;
		const double dwell = counted ? static_cast< double >( held ) / counted : 0.0;
		Note( fmt( "%s, 25 Hz (fc %.0f Hz) at +30 dB: the stroke sits on the stop %.1f %% of the time", t.p.model, t.fc, 100.0 * dwell ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --drift
//===========================================================================
int runDrift( const HarnessPerturb& hp )
{
	std::printf( "\n=== drift: an offset coil walks toward the Bl maximum below resonance and away above; a box pushes out\n"
	             "    (symmetry: odd with offset 0, so the steady state's mean is rounding; signs at second order, F_dc = b1 Bl0 |I|^2 (K - M w^2) / 2|Zm|^2;\n"
	             "     1 %% of Xlin proves it moved; the box's k2 <x^2> / Kt to 2 %%, over twice the next order)\n" );
	const int before = g_failures;
	for( int preset = 0; preset < kPresetCount && !stopEarly(); ++preset )
	{
		const Truth t = truthOf( preset, 0.0 );
		//Below resonance at 1.6 Xlin; above it at 1.0 Xlin -- harder than that
		//and the cone leaves the symmetric orbit by itself (reported below).
		const double hz[ 2 ]    = { std::max( 5.0, std::round( t.fs / 2.0 ) ), std::round( 2.0 * t.fs ) };
		const double level[ 2 ] = { 1.6, 1.0 };
		const double offsets[ 3 ] = { 0.0, 0.1, -0.1 };
		double mean[ 2 ][ 3 ];
		bool clean = true;
		for( int f = 0; f < 2; ++f )
			for( int o = 0; o < 3; ++o )
			{
				const Steady r = runSteady( { preset, 0.0, offsets[ o ], 0.0 }, hp, hz[ f ], amplitudeFor( t, hz[ f ], level[ f ] * t.xlin, preset ) );
				mean[ f ][ o ] = r.finite ? r.X[ 0 ].real() : HUGE_VAL;
				clean          = clean && r.hits == 0 && r.finite;
			}
		Check( clean, fmt( "%s: every drift run stayed off the stop", t.p.model ) );
		Check( std::fabs( mean[ 0 ][ 0 ] ) <= 1e-6 * t.xlin && std::fabs( mean[ 1 ][ 0 ] ) <= 1e-6 * t.xlin,
		       fmt( "(a) %s, offset 0: mean x %.2e Xlin at %g Hz (fs/2, 1.6 Xlin), %.2e at %g Hz (2 fs, 1.0 Xlin) (<= 1e-6)", t.p.model,
		            mean[ 0 ][ 0 ] / t.xlin, hz[ 0 ], mean[ 1 ][ 0 ] / t.xlin, hz[ 1 ] ) );
		Check( mean[ 0 ][ 1 ] <= -0.01 * t.xlin && mean[ 1 ][ 1 ] >= 0.01 * t.xlin,
		       fmt( "(b) %s, offset +0.1 Hg: mean x %+.4f Xlin at %g Hz (in, <= -0.01), %+.4f at %g Hz (out, >= +0.01)", t.p.model,
		            mean[ 0 ][ 1 ] / t.xlin, hz[ 0 ], mean[ 1 ][ 1 ] / t.xlin, hz[ 1 ] ) );
		const double m0 = std::fabs( mean[ 0 ][ 1 ] + mean[ 0 ][ 2 ] ) / std::fabs( mean[ 0 ][ 1 ] );
		const double m1 = std::fabs( mean[ 1 ][ 1 ] + mean[ 1 ][ 2 ] ) / std::fabs( mean[ 1 ][ 1 ] );
		Check( m0 <= 1e-6 && m1 <= 1e-6, fmt( "(c) %s, offset -0.1 Hg mirrors +0.1: %.1e and %.1e relative (<= 1e-6)", t.p.model, m0, m1 ) );

		const Truth b     = truthOf( preset, 1.2 );
		const double bhz  = std::max( 5.0, std::round( b.fc / 2.0 ) );
		const Steady r    = runSteady( { preset, 1.2, 0.0, 0.0 }, hp, bhz, amplitudeFor( b, bhz, 0.8 * b.plateau, preset ) );
		const double x1   = std::abs( r.X[ 1 ] );
		const double want = airSpringK2( b ) * ( x1 * x1 / 2.0 ) / b.kt;
		const double got  = r.X[ 0 ].real();
		Check( got > 0.0 && std::fabs( got / want - 1.0 ) <= 0.02,
		       fmt( "(d) %s, Qtc 1.2 box (Vb %.1f L), offset 0, %g Hz: mean x %+.4e mm; the air spring's k2 <x^2> / Kt predicts %+.4e (out; within 2 %%)",
		            b.p.model, b.vb * 1e3, bhz, got * 1e3, want * 1e3 ) );
	}
	if( !g_quiet )
	{
		//Reported, not claimed: above resonance a cone with NO offset, driven
		//a little harder, leaves the symmetric orbit and sits to one side.
		const Truth t   = truthOf( kDefaultPreset, 0.0 );
		const double hz = std::round( 2.0 * t.fs );
		const Steady r  = runSteady( { kDefaultPreset, 0.0, 0.0, 0.0 }, hp, hz, amplitudeFor( t, hz, 1.2 * t.xlin, kDefaultPreset ) );
		Note( fmt( "%s, offset 0, %g Hz at 1.2 Xlin: mean x %+.3f Xlin -- symmetric model, asymmetric orbit (the dynamic instability)", t.p.model, hz,
		           r.X[ 0 ].real() / t.xlin ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --surround (CPU: the profile, no GL)
//===========================================================================
/// The fraction of the driver's screen disc whose material was never printed.
double unpaintedFraction( Profile& profile, DriverModel& model, const Sample& s )
{
	std::vector< float > a( static_cast< size_t >( Profile::kBins ) * 4 ), b( a.size() );
	profile.Row( model, s, a.data(), b.data() );
	double total = 0.0, bare = 0.0;
	for( int k = 0; k < Profile::kBins; ++k )
	{
		const double w = k + 0.5;// area of an annulus goes as its radius
		total += w;
		if( a[ static_cast< size_t >( 4 * k + 3 ) ] < 0.5f )
			bare += w;
	}
	return bare / total;
}

int runSurround( const HarnessPerturb& hp )
{
	std::printf( "\n=== surround: the roll bends, it does not stretch; the hidden foot shows only at full inward stroke\n"
	             "    (3e-6: the polyline's chord deficit, dphi^2/24, bounds its length change; the arc itself is exact)\n" );
	const int before = g_failures;
	for( int preset = 0; preset < kPresetCount && !stopEarly(); ++preset )
	{
		const Truth t = truthOf( preset, 0.707 );
		Physics ph;
		setup( ph, { preset, 0.707, 0.1, 30.0 }, hp );
		Profile& profile = ph.plugin->ProfileForTest();
		ph.feeder.signal = mono( []( double tt ) { return std::sin( 2.0 * kPi * 25.0 * tt ); } );
		const double rest = profile.SurroundLengthAtRest();
		double worst      = 0.0;
		for( int f = 0; f < 60; ++f )
		{
			ph.Step();
			for( int k = 0; k < Profile::kRows; ++k )
			{
				const double back = 0.008 * DriverModel::kTapRate * ( k + 0.5 ) / Profile::kRows;
				const double len  = profile.SurroundLength( ph.Model(), ph.Model().At( back ) );
				worst             = std::max( worst, std::isfinite( len ) ? std::fabs( len / rest - 1.0 ) : HUGE_VAL );
			}
		}
		Check( worst <= 3e-6 && ph.Model().MaxAbsX() == t.xmech,
		       fmt( "%s, a full-stroke run (max |x| = %.4f Xmech): the roll's length changed by %.2e relative at worst (<= 3e-6)", t.p.model,
		            ph.Model().MaxAbsX() / t.xmech, worst ) );
		Sample s;
		const double atRest = unpaintedFraction( profile, ph.Model(), s );
		s.x                 = -t.xmech;
		const double inward = unpaintedFraction( profile, ph.Model(), s );
		s.x                 = t.xmech;
		const double outward = unpaintedFraction( profile, ph.Model(), s );
		Check( atRest == 0.0 && inward > 0.0, fmt( "%s: unpainted screen area %.2e at rest (0), %.2e at x = -Xmech (> 0); %.2e at +Xmech", t.p.model,
		                                           atRest, inward, outward ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// Picture checks.
//===========================================================================
/// Codes apart, at most, over the pixels `keep` accepts.
double worstCodes( const Floats& a, const Floats& b, int width, int height, const std::function< bool( int, int ) >& keep, int channels = 3,
                   int* where = nullptr )
{
	double worst = 0.0;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			if( !keep( x, y ) )
				continue;
			const size_t i = ( static_cast< size_t >( y ) * width + x ) * 4;
			for( int c = 0; c < channels; ++c )
			{
				const double d = std::fabs( a[ i + c ] - b[ i + c ] ) * 255.0;
				if( d > worst )
				{
					worst = d;
					if( where )
					{
						where[ 0 ] = x;
						where[ 1 ] = y;
					}
				}
			}
		}
	return worst;
}

/// Distance of a pixel centre from the frame centre, in frame heights.
double rhoOf( int x, int y, int width, int height )
{
	const double dx = x + 0.5 - 0.5 * width, dy = y + 0.5 - 0.5 * height;
	return std::sqrt( dx * dx + dy * dy ) / height;
}

void silence( Rig& rig )
{
	rig.feeder.signal = mono( []( double ) { return 0.0; } );
}

//===========================================================================
// --rest
//===========================================================================
int runRest( const HarnessPerturb& hp )
{
	std::printf( "\n=== rest: at silence the printed cone is the clip\n"
	             "    (1 code: the 8-bit quantisation; the rest map is the identity to 1e-8 of a frame height)\n" );
	const int before = g_failures;
	for( const Raster& raster : kRasters )
	{
		const Floats card = buildCard( raster.w, raster.h );
		{
			Rig rig;
			if( !rig.Init( raster.w, raster.h, &card ) )
				return 1;
			rig.Apply( hp );
			rig.Set( PT_AMBIENT, 1.0f );
			rig.Set( PT_GLOSS, 0.0f );
			rig.Set( PT_LAMP, 0.0f );
			silence( rig );
			if( !rig.Render( 30 ) )
				return 1;
			int at[ 2 ]        = { 0, 0 };
			const double worst = worstCodes( rig.Output(), card, raster.w, raster.h, []( int, int ) { return true; }, 4, at );
			Check( worst <= 1.0, fmt( "%dx%d, Ambient 1, Gloss 0, lamp off: largest difference from the clip %.3f codes (at %d,%d; <= 1)", raster.w,
			                          raster.h, worst, at[ 0 ], at[ 1 ] ) );
		}
		{
			Rig rig;
			if( !rig.Init( raster.w, raster.h, &card ) )
				return 1;
			rig.Apply( hp );
			silence( rig );
			if( !rig.Render( 30 ) )
				return 1;
			const double rhoMax = rig.plugin.ProfileForTest().RhoMax();
			const double lx = rig.plugin.LampX(), ly = rig.plugin.LampY();
			const double lampR = 0.008 * raster.h + 2.0;
			const double worst = worstCodes( rig.Output(), card, raster.w, raster.h, [ & ]( int x, int y ) {
				if( rhoOf( x, y, raster.w, raster.h ) < rhoMax + 1.5 / raster.h )
					return false;
				return std::hypot( x + 0.5 - lx, y + 0.5 - ly ) > lampR;
			} );
			Check( worst <= 1.0, fmt( "%dx%d, the defaults: outside the driver (and the lamp) the largest difference from the clip is %.3f codes (<= 1)",
			                          raster.w, raster.h, worst ) );
		}
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --magnify
//===========================================================================
/// The 50 % crossing of a bright disc's edge along a ray from the centre, in
/// pixels from the centre (pixel centres, linear between them).
double edgeAlong( const Floats& image, int width, int height, int dx, int dy )
{
	const double cx = 0.5 * width, cy = 0.5 * height;
	double prevD = -1.0, prevV = 1.0;
	for( int step = 0; step < std::max( width, height ); ++step )
	{
		//Walk pixel centres on the ray's own axis.
		const int x = dx != 0 ? static_cast< int >( cx ) + ( dx > 0 ? step : -1 - step ) : static_cast< int >( cx );
		const int y = dy != 0 ? static_cast< int >( cy ) + ( dy > 0 ? step : -1 - step ) : static_cast< int >( cy );
		if( x < 0 || y < 0 || x >= width || y >= height )
			break;
		const double d = dx != 0 ? std::fabs( x + 0.5 - cx ) : std::fabs( y + 0.5 - cy );
		const double v = pixelAt( image, width, x, y )[ 1 ];
		if( prevD >= 0.0 && prevV >= 0.5 && v < 0.5 )
			return prevD + ( d - prevD ) * ( prevV - 0.5 ) / ( prevV - v );
		prevD = d;
		prevV = v;
	}
	return -1.0;
}

int runMagnify( const HarnessPerturb& hp )
{
	std::printf( "\n=== magnify: a DC input holds the cone out by Bl V / (Re Kms), and a ring printed on it grows as the pinhole says\n"
	             "    (x_dc to 1e-6; the ring to 0.5 px, a resampled step's half-level crossing; the bin is < 1 px at both rasters)\n" );
	const int before = g_failures;
	const int preset = kDefaultPreset;
	const Truth t    = truthOf( preset, 0.0 );
	const double target = 0.9 * t.plateau;
	//The harness's own pinhole (SPEC-woofer.md): camera 1 diameter away.
	const double size = 0.95, camera = 1.0;
	const double dcam = camera * 2.0 * t.p.overallRadius;
	const double kp   = 0.5 * size * dcam / t.p.overallRadius;
	const double rvc = t.p.vcRadius, rdc = 1.2 * rvc;
	const double rm  = 0.5 * ( rdc + t.p.coneRadius );
	const double z0  = -( t.p.coneRadius - rm ) * std::tan( 25.0 * kPi / 180.0 );
	const double rho = kp * rm / ( dcam - z0 );
	for( const Raster& raster : kRasters )
	{
		//The clip: a white disc of radius rho on black, its edge antialiased by
		//16x supersampling so its half-level crossing sits on the edge.
		Floats card( static_cast< size_t >( raster.w ) * raster.h * 4 );
		for( int y = 0; y < raster.h; ++y )
			for( int x = 0; x < raster.w; ++x )
			{
				int inside = 0;
				for( int sy = 0; sy < 4; ++sy )
					for( int sx = 0; sx < 4; ++sx )
					{
						const double px = x + ( sx + 0.5 ) / 4.0 - 0.5 * raster.w, py = y + ( sy + 0.5 ) / 4.0 - 0.5 * raster.h;
						inside += std::sqrt( px * px + py * py ) < rho * raster.h ? 1 : 0;
					}
				float* o = &card[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
				o[ 0 ] = o[ 1 ] = o[ 2 ] = inside / 16.0f;
				o[ 3 ]                   = 1.0f;
			}
		Rig rig;
		if( !rig.Init( raster.w, raster.h, &card ) )
			return 1;
		rig.Apply( hp );
		rig.Set( PT_DRIVER, static_cast< float >( preset ) );
		rig.Set( PT_BOX, 0.0f );
		rig.Set( PT_OFFSET, ParamFromOffset( 0.0 ) );
		rig.Set( PT_DRIVE, ParamFromDrive( 0.0 ) );
		rig.Set( PT_SIZE, ParamFromSize( size ) );
		rig.Set( PT_CAMERA, ParamFromCamera( camera ) );
		rig.Set( PT_AMBIENT, 1.0f );
		rig.Set( PT_GLOSS, 0.0f );
		rig.Set( PT_LAMP, 0.0f );
		rig.Set( PT_EXPOSURE, 0.0f );
		rig.Set( PT_RIPPLE, 0.0f );
		const double level = target * t.p.Re * t.kt / ( t.p.Bl * voltsPerUnit( preset ) );
		rig.feeder.signal  = mono( [ = ]( double tt ) { return level * fadeIn( tt, 1.0 ); } );
		if( !rig.Render( 180 ) )
			return 1;
		const DriverModel& model = rig.plugin.ModelForTest();
		const double volts       = rig.plugin.VoltsForTest().empty() ? 0.0 : rig.plugin.VoltsForTest().back();
		const double xdc         = t.p.Bl * volts / ( t.p.Re * t.kt );
		const double x           = model.Newest().x;
		Check( std::fabs( x / xdc - 1.0 ) <= 1e-6, fmt( "%dx%d: %s held at %.6f mm by %.4f V; Bl V / (Re Kms) = %.6f mm (%.1e relative, <= 1e-6)",
		                                                raster.w, raster.h, t.p.model, x * 1e3, volts, xdc * 1e3, std::fabs( x / xdc - 1.0 ) ) );
		const double want  = rho * ( dcam - z0 ) / ( dcam - z0 - x ) * raster.h;
		const Floats image = rig.Output();
		const int dirs[ 4 ][ 2 ] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		double worst             = 0.0;
		std::string got;
		for( const auto& d : dirs )
		{
			const double r = edgeAlong( image, raster.w, raster.h, d[ 0 ], d[ 1 ] );
			worst          = std::max( worst, r < 0.0 ? HUGE_VAL : std::fabs( r - want ) );
			got += fmt( " %.2f", r );
		}
		Check( worst <= 0.5, fmt( "%dx%d: the ring printed at %.2f px appears at%s px; the pinhole predicts %.2f (within 0.5 px)", raster.w, raster.h,
		                          rho * raster.h, got.c_str(), want ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --alpha
//===========================================================================
int runAlpha( const HarnessPerturb& hp )
{
	std::printf( "\n=== alpha: the driver is opaque -- output alpha is mix( clip alpha, 1, Mix ); where the clip is clear the bare cone shows\n"
	             "    (1 code)\n" );
	const int before = g_failures;
	for( const Raster& raster : kRasters )
	{
		//Clear on the left half, a ramp to opaque across the right.
		auto withAlpha = [ & ]( Floats card ) {
			for( int y = 0; y < raster.h; ++y )
				for( int x = 0; x < raster.w; ++x )
					card[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 + 3 ] =
						x < raster.w / 2 ? 0.0f : static_cast< float >( ( x - raster.w / 2 + 0.5 ) / ( raster.w / 2 ) );
			return card;
		};
		const Floats card    = withAlpha( buildCard( raster.w, raster.h ) );
		const Floats magenta = withAlpha( flatCard( raster.w, raster.h, 1.0f, 0.0f, 1.0f ) );
		Floats atMixOne[ 2 ];
		for( float mix : { 0.0f, 0.5f, 1.0f } )
		{
			Rig rig;
			if( !rig.Init( raster.w, raster.h, &card ) )
				return 1;
			rig.Apply( hp );
			rig.Set( PT_MIX, mix );
			silence( rig );
			if( !rig.Render( 10 ) )
				return 1;
			const Floats out = rig.Output();
			double worst     = 0.0;
			for( size_t i = 3; i < out.size(); i += 4 )
				worst = std::max( worst, std::fabs( out[ i ] - ( card[ i ] + ( 1.0 - card[ i ] ) * mix ) ) * 255.0 );
			Check( worst <= 1.0, fmt( "%dx%d, Mix %.1f: output alpha against mix( clip alpha, 1, Mix ): %.3f codes at worst (<= 1)", raster.w, raster.h,
			                          mix, worst ) );
			if( mix == 1.0f )
				atMixOne[ 0 ] = out;
		}
		{
			Rig rig;
			if( !rig.Init( raster.w, raster.h, &magenta ) )
				return 1;
			rig.Apply( hp );
			silence( rig );
			if( !rig.Render( 10 ) )
				return 1;
			atMixOne[ 1 ] = rig.Output();
		}
		const double lx = 0, ly = 0;
		(void)lx;
		(void)ly;
		const double worst = worstCodes( atMixOne[ 0 ], atMixOne[ 1 ], raster.w, raster.h, [ & ]( int x, int ) { return x < raster.w / 2 - 4; } );
		Check( worst <= 1.0, fmt( "%dx%d, Mix 1: where the clip is clear, the card and a magenta clip give the same picture (%.3f codes apart; <= 1)",
		                          raster.w, raster.h, worst ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --resize
//===========================================================================
int runResize( const HarnessPerturb& hp )
{
	std::printf( "\n=== resize: a new raster mid-run changes only the drawing\n"
	             "    (exact: the model's state is on the CPU and in metres; the comparison is within one context. On Apple's software\n"
	             "     renderer the picture within 0.001 codes: under load it has drawn the same frame a few float ulps apart)\n" );
	const int before = g_failures;
	for( const Raster& raster : kRasters )
	{
		const int w2 = raster.w * 3 / 4, h2 = raster.h * 3 / 4;
		Rig moved, fixed;
		if( !moved.Init( raster.w, raster.h ) || !fixed.Init( w2, h2 ) )
			return 1;
		for( Rig* r : { &moved, &fixed } )
		{
			r->Apply( hp );
			r->Set( PT_DRIVE, ParamFromDrive( 12.0 ) );
			r->feeder.signal = mono( musicAt );
		}
		if( !moved.Render( 60 ) || !fixed.Render( 60 ) )
			return 1;
		if( !moved.Resize( w2, h2 ) )
			return 1;
		if( !moved.Render( 1 ) || !fixed.Render( 1 ) )
			return 1;
		const DriverModel& a = moved.plugin.ModelForTest();
		const DriverModel& b = fixed.plugin.ModelForTest();
		Check( a.Count() == b.Count() && a.Newest().x == b.Newest().x && moved.x == fixed.x,
		       fmt( "%dx%d -> %dx%d after 60 frames: the trace is unbroken (%llu samples against %llu, newest x %.6f mm against %.6f)", raster.w,
		            raster.h, w2, h2, static_cast< unsigned long long >( a.Count() ), static_cast< unsigned long long >( b.Count() ), a.Newest().x * 1e3,
		            b.Newest().x * 1e3 ) );
		const Floats ma = moved.Output(), fb = fixed.Output();
		int at[ 2 ]  = { -1, -1 };
		const double worst = worstCodes( ma, fb, w2, h2, []( int, int ) { return true; }, 4, at );
		int differ = 0;
		for( size_t i = 0; i < ma.size(); i += 4 )
			differ += ( ma[ i ] != fb[ i ] || ma[ i + 1 ] != fb[ i + 1 ] || ma[ i + 2 ] != fb[ i + 2 ] || ma[ i + 3 ] != fb[ i + 3 ] ) ? 1 : 0;
		//The software renderer is not bit-reproducible under load: with 14 runs
		//at once on 16 cores, 2 of 28 drew 3 and 17 pixels 5.5e-7 and 6.1e-5
		//codes apart (2.4e-4 seen in verify), with the trace and every uniform
		//identical. A real resize bug (a stale size, a reset) is whole codes.
		const double allowed = g_software ? 1e-3 : 0.0;
		Check( worst <= allowed, fmt( "%dx%d: the next frame equals a run at %dx%d throughout (%.3g codes apart, %d pixels differ, worst at %d,%d; %s)", w2,
		                              h2, w2, h2, worst, differ, at[ 0 ], at[ 1 ], g_software ? "<= 0.001 on the software renderer" : "0" ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --fallback
//===========================================================================
/// The strongest frequency in x[from .. from + n) between lo and hi Hz (1 Hz
/// steps over a 1 s window: one DFT bin each).
double dominantHz( const Doubles& x, size_t from, size_t n, double lo, double hi )
{
	double mean = 0.0;
	for( size_t k = 0; k < n; ++k )
		mean += x[ from + k ];
	mean /= static_cast< double >( n );
	Doubles centred( n );
	for( size_t k = 0; k < n; ++k )
		centred[ k ] = x[ from + k ] - mean;
	const double step = static_cast< double >( DriverModel::kTapRate ) / static_cast< double >( n );
	double best = 0.0, bestHz = 0.0;
	for( double hz = std::ceil( lo / step ) * step; hz <= hi; hz += step )
	{
		const double m = std::abs( dftAt( centred, 0, n, hz, DriverModel::kTapRate, 0.0 ) );
		if( m > best )
		{
			best   = m;
			bestHz = hz;
		}
	}
	return bestHz;
}

struct FallbackRun
{
	double peak = 0, hz = 0;
	int samples = 0, frames = 0;
	bool tapEmpty = true, host = true;
	double lamp[ 4 ] = {};
	double clockScale = 0;
};

/// Host bins only (no device, nothing fed): `bins( frame, element )`.
FallbackRun runHostBins( const Raster& raster, const HarnessPerturb& hp, std::function< float( int, int ) > bins, bool msClock )
{
	FallbackRun out;
	Rig rig;
	rig.presetClock = !msClock || hp.clockAsSeconds;
	if( msClock )
	{
		rig.timeOffset = 4.99e8;
		rig.timeScale  = 1000.0;
	}
	if( !rig.Init( raster.w, raster.h ) )
		return out;
	rig.Apply( hp );
	rig.Set( PT_FFT_LAW, 0.0f );
	rig.beforeFrame = [ & ]( int frame ) {
		for( int e = 0; e < 64; ++e )
			rig.plugin.SetParamElementValue( PT_AUDIO, static_cast< unsigned int >( e ), bins( frame, e ) );
	};
	if( !rig.Render( 120 ) )
		return out;
	const size_t mark  = rig.x.size();
	const int counted  = rig.samples;
	if( !rig.Render( 120 ) )
		return out;
	out.samples = rig.samples - counted;
	out.frames  = 120;
	for( size_t k = mark; k < rig.x.size(); ++k )
		out.peak = std::max( out.peak, std::fabs( rig.x[ k ] ) );
	if( rig.x.size() >= mark + DriverModel::kTapRate )
		out.hz = dominantHz( rig.x, rig.x.size() - DriverModel::kTapRate, DriverModel::kTapRate, 5.0, 1000.0 );
	const earshot::Frame& f = rig.plugin.AudioForTest().Frame();
	out.tapEmpty            = f.tapAvailable == 0 && !rig.plugin.AudioForTest().FromDevice();
	out.host                = f.source == earshot::Source::Host;
	const Floats lamp       = rig.Region( static_cast< int >( rig.plugin.LampX() ), static_cast< int >( rig.plugin.LampY() ), 1, 1 );
	for( int c = 0; c < 4; ++c )
		out.lamp[ c ] = lamp[ static_cast< size_t >( c ) ] * 255.0;
	out.clockScale = rig.plugin.ClockScale();
	return out;
}

int runFallback( const HarnessPerturb& hp )
{
	std::printf( "\n=== fallback: no device -- Resolume's FFT bands become sines at 63, 400, 1600 and 6400 Hz; it pumps, it is not the waveform\n"
	             "    (one DFT bin; the high bands' share follows 1/f^2, (63/6400)^2 ~ 1e-4, so 1 %% has a margin of 100)\n" );
	const int before = g_failures;
	auto lowPulse    = []( int frame, int e ) { return e == 0 && ( frame / 15 ) % 2 == 0 ? 0.8f : 0.0f; };
	auto highPulse   = []( int frame, int e ) { return e >= 10 && e <= 36 && ( frame / 15 ) % 2 == 0 ? 0.8f : 0.0f; };
	for( const Raster& raster : kRasters )
	{
		const Truth t = truthOf( kDefaultPreset, 0.707 );
		for( bool ms : { false, true } )
		{
			if( stopEarly() )
				break;
			const FallbackRun low = runHostBins( raster, hp, lowPulse, ms );
			const char* clock     = ms ? "host clock in ms at 4.99e8" : "host clock in seconds";
			Check( low.host && low.tapEmpty && low.peak >= 0.01 * t.xmech,
			       fmt( "%dx%d, %s: on the host's bins (tap empty: %s) bin 0 pulsing pumps the cone to %.3f mm (%.1f %% of Xmech; >= 1 %%)", raster.w,
			            raster.h, clock, low.tapEmpty ? "yes" : "no", low.peak * 1e3, 100.0 * low.peak / t.xmech ) );
			Check( std::fabs( low.hz - 63.25 ) <= 1.0, fmt( "%dx%d, %s: the trace's strongest frequency is %.0f Hz (63.2 within one bin)", raster.w,
			                                                raster.h, clock, low.hz ) );
			const double want = low.frames / 60.0 * DriverModel::kTapRate;
			Check( std::fabs( low.samples - want ) <= 1.0 && ( !ms || low.clockScale == 0.001 ),
			       fmt( "%dx%d, %s: %d tap samples integrated over %d frames of 1/60 s (%.0f; within 1); clock settled as %g s a unit", raster.w,
			            raster.h, clock, low.samples, low.frames, want, low.clockScale ) );
			Check( std::fabs( low.lamp[ 0 ] - 255.0 ) <= 2.0 && std::fabs( low.lamp[ 1 ] - 0.62 * 255.0 ) <= 2.0 && std::fabs( low.lamp[ 2 ] - 0.05 * 255.0 ) <= 2.0,
			       fmt( "%dx%d, %s: the status lamp is amber (%.0f, %.0f, %.0f)", raster.w, raster.h, clock, low.lamp[ 0 ], low.lamp[ 1 ], low.lamp[ 2 ] ) );
			if( ms )
				continue;
			const FallbackRun high = runHostBins( raster, hp, highPulse, false );
			Check( high.peak <= 0.01 * low.peak, fmt( "%dx%d: the top band alone (bins 10-36, its level clamped to 1) moves it %.2e mm, %.2e of bin 0's (<= 1 %%)",
			                                          raster.w, raster.h, high.peak * 1e3, high.peak / low.peak ) );
		}
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --hostbins: Resolume's own FFT, earshot's host path alone, its bins built
// the way Arena 7.29.0 was measured to build them (earshot's AGENTS.md,
// "Resolume's 64 bins, measured") and handed to the plugin as its
// FF_USAGE_FFT buffer, element by element, as a host does; nothing goes
// through feed(). Under earshot 8124191 (bins read as linear to 22.05 kHz,
// each given whole to the band holding its centre) the 20-200 Hz band got all
// of bin 0, 0-375 Hz, and the cone was driven 1/0.693 as hard as its share.
//===========================================================================

/// Arena's FFT buffer as measured (snowglobe's sgtest, unchanged): every 2048
/// samples of the engine's rate, a 2048-point Hann-windowed DFT of the newest
/// 2048 samples, and bin k the sum of |X_i| / 2N over the 16 fine bins
/// 16k .. 16k + 15. That is linear, fs / 128 a bin (375 Hz at 48 kHz) to
/// fs / 2, a magnitude, a sine of amplitude A reading about A / 4 in its bin.
/// Held between updates, as Arena holds it.
struct HostSpectrum
{
	static constexpr int kN = 2048;
	int rate;
	std::function< double( double ) > signal;
	std::vector< float > bins = std::vector< float >( 64, 0.0f );
	int64_t updated           = -1;

	HostSpectrum( int r, std::function< double( double ) > s ) : rate( r ), signal( std::move( s ) ) {}

	const std::vector< float >& at( double t )
	{
		const int64_t u = static_cast< int64_t >( std::floor( t * rate / kN + 1e-9 ) );
		if( u == updated )
			return bins;
		updated = u;
		std::vector< Complex > x( kN );
		for( int n = 0; n < kN; ++n )
		{
			const int64_t sample = u * kN - kN + n;
			const double hann    = 0.5 - 0.5 * std::cos( 2.0 * kPi * n / kN );
			x[ static_cast< size_t >( n ) ] = hann * signal( static_cast< double >( sample ) / rate );
		}
		//Radix-2, in place.
		for( int i = 1, j = 0; i < kN; ++i )
		{
			int bit = kN >> 1;
			for( ; j & bit; bit >>= 1 )
				j ^= bit;
			j ^= bit;
			if( i < j )
				std::swap( x[ static_cast< size_t >( i ) ], x[ static_cast< size_t >( j ) ] );
		}
		for( int len = 2; len <= kN; len <<= 1 )
		{
			const Complex w = std::polar( 1.0, -2.0 * kPi / len );
			for( int i = 0; i < kN; i += len )
			{
				Complex wk( 1.0, 0.0 );
				for( int k = 0; k < len / 2; ++k )
				{
					const Complex a = x[ static_cast< size_t >( i + k ) ], b = x[ static_cast< size_t >( i + k + len / 2 ) ] * wk;
					x[ static_cast< size_t >( i + k ) ]           = a + b;
					x[ static_cast< size_t >( i + k + len / 2 ) ] = a - b;
					wk *= w;
				}
			}
		}
		for( int k = 0; k < 64; ++k )
		{
			double sum = 0.0;
			for( int i = 16 * k; i < 16 * k + 16; ++i )
				sum += std::abs( x[ static_cast< size_t >( i ) ] );
			bins[ static_cast< size_t >( k ) ] = static_cast< float >( sum / ( 2.0 * kN ) );
		}
		return bins;
	}
};

/// One frame of the host: its bins into the Audio buffer, then the frame.
void hostStep( Physics& ph, HostSpectrum& host )
{
	const std::vector< float >& b = host.at( ph.frame / ph.fps );
	for( unsigned int e = 0; e < 64; ++e )
		ph.plugin->SetParamElementValue( PT_AUDIO, e, b[ e ] );
	ph.Step();
}

/// 128 BPM (Arena's Beat 001), so the kicks fall at every phase of the host's
/// 2048-sample updates: wftest's kick on every beat, and its hat (high-passed
/// at 7 kHz, so it puts nothing in bin 0) on the eighths between.
constexpr double kHostBeat = 60.0 / 128.0;
double hostLoopAt( double t, bool kicks )
{
	if( t < 0.0 )
		return 0.0;
	double s = 0.25 * hatAt( std::fmod( t + kHostBeat / 2.0, kHostBeat ) );
	if( kicks )
		s += 0.8 * kickAt( std::fmod( t, kHostBeat ) ) / kickPeak();
	return s;
}

int runHostFft( const HarnessPerturb& hp )
{
	std::printf( "\n=== hostbins: Resolume's FFT alone, its 64 bins built as Arena 7.29.0 was measured to build them (2048-point Hann, 16 fine bins a bin,\n"
	             "    held for 2048 samples), nothing fed, at 48 and 44.1 kHz: earshot reads the bins at the host's rate; the 20-200 Hz band (the 63 Hz\n"
	             "    sine) takes 180 Hz of bin 0's span, the 200-800 Hz band the rest above 200 Hz; the cone's 63 Hz stroke is the closed form's for\n"
	             "    that share; every kick of a 128 BPM loop pumps the cone and its hats alone don't\n"
	             "    (the share to 1e-3, float arithmetic on the same bins; the split and the stroke to 1 %%: the 1 s DFT of a non-integer\n"
	             "    frequency and the 400 Hz band's leakage are under 0.2 %%, and 8124191's rule is 44 %% off; the loop's hats: 1 %%)\n" );
	const int before    = g_failures;
	const Truth t       = truthOf( kDefaultPreset, 0.707 );
	const double driveDb = 18.0;
	const double volts  = voltsPerUnit( kDefaultPreset ) * std::pow( 10.0, driveDb / 20.0 );
	const double hz0    = std::sqrt( 20.0 * 200.0 );//the band's sine, as Audio.cpp makes it
	for( int rate : { 48000, 44100 } )
	{
		if( stopEarly() )
			break;
		const double binHz = rate / 128.0;
		const double share = 180.0 / binHz;//20-200 Hz of bin 0's [0, binHz)
		//(a) A steady 100 Hz tone: all of it in bin 0, all of it in band 0.
		{
			Physics ph;
			setup( ph, { kDefaultPreset, 0.707, 0.0, driveDb }, hp );
			ph.Set( PT_FFT_LAW, 0.0f );
			ph.plugin->SetSampleRate( static_cast< unsigned int >( rate ) );
			HostSpectrum host( rate, []( double s ) { return 0.25 * fadeIn( s, 0.5 ) * std::sin( 2.0 * kPi * 100.0 * s ); } );
			const int frames = 4 * 60;
			double worstShare = 0.0, worstSplit = 0.0, level0 = 0.0, bin0 = 0.0;
			int counted = 0;
			for( int f = 0; f < frames; ++f )
			{
				hostStep( ph, host );
				if( f < frames - 60 )
					continue;
				const earshot::Frame& fr = ph.plugin->AudioForTest().Frame();
				const double b0 = host.bins[ 0 ], l0 = fr.bands[ 0 ].level, l1 = fr.bands[ 1 ].level;
				worstShare = std::max( worstShare, std::fabs( l0 / b0 / std::sqrt( share ) - 1.0 ) );
				worstSplit = std::max( worstSplit, std::fabs( ( l0 * l0 + l1 * l1 ) / ( b0 * b0 ) / ( ( binHz - 20.0 ) / binHz ) - 1.0 ) );
				level0 += l0;
				bin0 += b0;
				++counted;
			}
			level0 /= counted;
			bin0 /= counted;
			const double hostRate = ph.plugin->AudioForTest().HostRateForTest();
			Check( std::fabs( hostRate - rate ) < 1e-9, fmt( "%d Hz: the host says %d Hz and earshot reads its bins at %.0f Hz (%.2f Hz a bin)", rate, rate,
			                                                 hostRate, binHz ) );
			Check( worstShare <= 1e-3, fmt( "%d Hz, a 100 Hz tone: the 20-200 Hz band's level is sqrt(180/%.2f) = %.4f of bin 0's in every frame of the last "
			                                "second (worst %.1e off; 8124191 gave it 1)",
			                                rate, binHz, std::sqrt( share ), worstShare ) );
			Check( worstSplit <= 0.01, fmt( "%d Hz: bin 0 split between the 20-200 and 200-800 Hz bands, nothing lost or counted twice: their energies sum to "
			                                "%.4f of the bin's (want %.4f, the bin less its 0-20 Hz; worst %.1e off)",
			                                rate, ( binHz - 20.0 ) / binHz, ( binHz - 20.0 ) / binHz, worstSplit ) );
			const size_t n    = DriverModel::kTapRate;
			const double got  = std::abs( dftBin( ph.x, ph.x.size() - n, n, hz0, DriverModel::kTapRate ) );
			const double want = std::abs( closedFormOf( t, hz0 ) ) * volts * std::sqrt( share ) * bin0;
			Check( std::fabs( got / want - 1.0 ) <= 0.01,
			       fmt( "%d Hz: the cone's %.1f Hz stroke %.4f mm, the closed form's for the band's share of bin 0 at +%.0f dB %.4f mm (%.4f; within 1 %%), "
			            "%.1f %% of Xmech: %.3f of what bin 0 whole would drive",
			            rate, hz0, got * 1e3, driveDb, want * 1e3, got / want, 100.0 * got / t.xmech, std::sqrt( share ) ) );
		}
		//(b) The loop: every kick pumps the cone; at 48 kHz, its hats alone don't.
		double loopPeak = 0.0;
		for( bool kicks : { true, false } )
		{
			if( !kicks && rate != 48000 )
				continue;
			Physics ph;
			setup( ph, { kDefaultPreset, 0.707, 0.0, driveDb }, hp );
			ph.Set( PT_FFT_LAW, 0.0f );
			ph.plugin->SetSampleRate( static_cast< unsigned int >( rate ) );
			HostSpectrum host( rate, [ kicks ]( double s ) { return hostLoopAt( s, kicks ); } );
			const int frames = 7 * 60;
			for( int f = 0; f < frames; ++f )
				hostStep( ph, host );
			//Trace sample i is at about i / kTapRate s (the fallback counts
			//samples off the clock). Beats 3.. whose whole window is inside 7 s.
			std::vector< double > peaks;
			double all = 0.0;
			for( int k = 3;; ++k )
			{
				const size_t from = static_cast< size_t >( std::llround( k * kHostBeat * DriverModel::kTapRate ) );
				const size_t to   = static_cast< size_t >( std::llround( ( k + 1 ) * kHostBeat * DriverModel::kTapRate ) );
				if( to > ph.x.size() )
					break;
				double p = 0.0;
				for( size_t i = from; i < to; ++i )
					p = std::max( p, std::fabs( ph.x[ i ] ) );
				peaks.push_back( p );
				all = std::max( all, p );
			}
			if( kicks )
			{
				loopPeak       = all;
				double least   = HUGE_VAL;
				for( double p : peaks )
					least = std::min( least, p );
				Check( !peaks.empty() && all >= 0.01 * t.xmech && least >= 0.5 * all,
				       fmt( "%d Hz, kicks and hats at 128 BPM: every one of %zu beats pumps the cone, the least %.1f %% of Xmech, the most %.1f %% "
				            "(each >= half the most, the most >= 1 %%)",
				            rate, peaks.size(), 100.0 * least / t.xmech, 100.0 * all / t.xmech ) );
			}
			else
				Check( all <= 0.01 * loopPeak, fmt( "%d Hz, the hats alone: the cone's peak %.2e mm, %.1e of the loop's (<= 1 %%)", rate, all * 1e3,
				                                    loopPeak > 0.0 ? all / loopPeak : HUGE_VAL ) );
		}
	}
	//A rate no engine runs at is not one: earshot keeps its 48 kHz.
	{
		Physics ph;
		setup( ph, { kDefaultPreset, 0.707, 0.0, driveDb }, hp );
		ph.plugin->SetSampleRate( 0u );
		ph.Step();
		ph.Step();
		const double hostRate = ph.plugin->AudioForTest().HostRateForTest();
		Check( std::fabs( hostRate - 48000.0 ) < 1e-9, fmt( "a host rate of 0: earshot keeps %.0f Hz (want 48000)", hostRate ) );
	}
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// --devices
//===========================================================================
bool lampIs( const double got[ 4 ], double r, double g, double b, double slack = 2.0 )
{
	return std::fabs( got[ 0 ] - r * 255.0 ) <= slack && std::fabs( got[ 1 ] - g * 255.0 ) <= slack && std::fabs( got[ 2 ] - b * 255.0 ) <= slack;
}

int runDevices( const HarnessPerturb& hp )
{
	std::printf( "\n=== devices: the menu, the states and the lamp, on a scripted capture backend (no audio device is touched)\n"
	             "    (100 ms: a third of the scripted open; a wall-time limit measures the scheduler, so it is generous)\n" );
	const int before = g_failures;
	const Raster raster = kRasters.front();
	{
		std::lock_guard< std::mutex > lock( g_deviceMutex );
		g_deviceNames = { "Test Mic", "Test Silent", "Test Slow" };
	}
	Rig rig;
	//The device path takes all the tap queued, up to a second a frame, and the
	//scripted backend runs at 4x real time: on the software renderer one frame
	//outgrew the trace. Nothing here reads x; max |x| and the stop hits are
	//counted per substep in the model, so no sample goes unchecked.
	rig.gapsAllowed = true;
	if( !rig.Init( raster.w, raster.h ) )
		return 1;
	rig.Apply( hp );
	//Every control the lamp depends on, set here. At the default Drive (+18 dB)
	//Test Mic's 50 Hz sine at half scale drives the 30 cm onto the stop, and
	//each hit flashes the lamp white for 80 ms of audio: on the GPU the reads
	//happened to miss the flashes, on the software renderer (slower frames,
	//more audio between them) they did not. At 0 dB the stroke stays well
	//under Xlin and the lamp shows only the input's state.
	rig.Set( PT_DRIVER, static_cast< float >( kDefaultPreset ) );
	rig.Set( PT_DRIVE, ParamFromDrive( 0.0 ) );
	rig.Set( PT_LAMP, 1.0f );
	auto press = [ & ]( unsigned int id ) {
		rig.Set( id, 1.0f );
		rig.Render( 1 );
		rig.Set( id, 0.0f );
	};
	auto lamp = [ & ]( double out[ 4 ] ) {
		const Floats p = rig.Region( static_cast< int >( rig.plugin.LampX() ), static_cast< int >( rig.plugin.LampY() ), 1, 1 );
		for( int c = 0; c < 4; ++c )
			out[ c ] = p[ static_cast< size_t >( c ) ] * 255.0;
	};
	auto waitFor = [ & ]( std::function< bool() > done, double seconds ) {
		const auto start = std::chrono::steady_clock::now();
		while( std::chrono::duration< double >( std::chrono::steady_clock::now() - start ).count() < seconds )
		{
			rig.Render( 1 );
			if( done() )
				return true;
			std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
		}
		return false;
	};
	auto state = [ & ]() { return rig.plugin.AudioForTest().State(); };
	if( hp.menuByIndex )
		press( PT_RESCAN );

	//(a) The menu.
	{
		const char* names[] = { "None (host FFT)", "System default", "Test Mic", "Test Silent", "Test Slow" };
		const float keys[]  = { 0.0f, earshot::deviceKey( earshot::kDefaultDevice ), earshot::deviceKey( "Test Mic" ), earshot::deviceKey( "Test Silent" ),
                               earshot::deviceKey( "Test Slow" ) };
		bool ok             = rig.plugin.GetNumParamElements( PT_INPUT ) == static_cast< unsigned int >( kInputSlots );
		std::string shown;
		for( unsigned int e = 0; e < rig.plugin.GetNumParamElements( PT_INPUT ); ++e )
		{
			const std::string label = rig.plugin.GetParamElementName( PT_INPUT, e );
			const float value       = elementValue( rig.plugin, PT_INPUT, e );
			if( e < 5 )
			{
				ok = ok && label == names[ e ] && value == keys[ e ];
				shown += fmt( " \"%s\"=%.0f", label.c_str(), value );
			}
			else
				ok = ok && label == "(none)" && value == 0.0f;
		}
		Check( ok, fmt( "(a) the menu, by name and key:%s, then %d \"(none)\" = 0", shown.c_str(), kInputSlots - 5 ) );
	}
	//(b) A device that delivers.
	float value = 0.0f;
	optionValue( rig.plugin, PT_INPUT, "Test Mic", value );
	rig.Set( PT_INPUT, value );
	{
		const bool running = waitFor( [ & ] { return state() == earshot::CaptureState::Running && rig.plugin.AudioForTest().FromDevice(); }, 3.0 );
		rig.Render( 1 );
		double c[ 4 ];
		lamp( c );
		const auto hits = static_cast< unsigned long long >( rig.plugin.ModelForTest().StopHits() );
		Check( running && lampIs( c, 0.15, 1.0, 0.25 ) && hits == 0,
		       fmt( "(b) Test Mic: %s, the lamp green (%.0f, %.0f, %.0f); %llu stop hits (0: no white flash can stand in)", earshot::toString( state() ),
		            c[ 0 ], c[ 1 ], c[ 2 ], hits ) );
	}
	//(c) A slow open never holds up the render thread.
	optionValue( rig.plugin, PT_INPUT, "Test Slow", value );
	{
		//The frame's own cost is the renderer's (51 ms at 320x180 on Apple's
		//software renderer, 2.7 ms on the GPU), so the limit is 100 ms OVER the
		//longest of the frames just before the select: an open on the render
		//thread would add its whole 300 ms.
		auto timedFrame = [ & ]() {
			const auto t0 = std::chrono::steady_clock::now();
			rig.Render( 1 );
			glFinish();
			return std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - t0 ).count();
		};
		double baseline = 0.0;
		for( int k = 0; k < 10; ++k )
			baseline = std::max( baseline, timedFrame() );
		rig.Set( PT_INPUT, value );
		double longest   = 0.0;
		const auto start = std::chrono::steady_clock::now();
		while( std::chrono::duration< double >( std::chrono::steady_clock::now() - start ).count() < 0.6 )
		{
			longest = std::max( longest, timedFrame() );
			std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
		}
		const bool opened = waitFor( [ & ] { return state() == earshot::CaptureState::Running; }, 3.0 );
		Check( longest <= baseline + 100.0 && opened,
		       fmt( "(c) Test Slow (a 300 ms open): the longest frame meanwhile took %.1f ms, the longest before it %.1f (<= 100 more); then %s", longest,
		            baseline, earshot::toString( state() ) ) );
	}
	//(d) A device that delivers zeros.
	optionValue( rig.plugin, PT_INPUT, "Test Silent", value );
	rig.Set( PT_INPUT, value );
	{
		const bool silent = waitFor( [ & ] { return state() == earshot::CaptureState::Silent; }, 6.0 );
		double c1[ 4 ], c2[ 4 ];
		rig.Render( 1 );
		lamp( c1 );
		rig.Render( 30 );
		lamp( c2 );
		Check( silent && lampIs( c1, 1.0, 0.05, 0.02 ) && lampIs( c2, 1.0, 0.05, 0.02 ),
		       fmt( "(d) Test Silent: %s after 3 s of zeros, the lamp steady red (%.0f, %.0f, %.0f) and half a second later (%.0f, %.0f, %.0f)",
		            earshot::toString( state() ), c1[ 0 ], c1[ 1 ], c1[ 2 ], c2[ 0 ], c2[ 1 ], c2[ 2 ] ) );
	}
	//(e) A saved device that is not here.
	const float goneKey = earshot::deviceKey( "Gone Mic" );
	const int opensBefore = g_opens.load();
	rig.Set( PT_INPUT, goneKey );
	{
		const bool missing = waitFor( [ & ] { return state() == earshot::CaptureState::Missing; }, 3.0 );
		earshot::Status status;
		rig.plugin.AudioForTest().Engine().status( status );
		double c1[ 4 ], c2[ 4 ];
		rig.Render( 1 );
		lamp( c1 );
		rig.Render( 30 );
		lamp( c2 );
		const bool blinks = std::fabs( c1[ 0 ] - c2[ 0 ] ) >= 100.0 && std::min( c1[ 0 ], c2[ 0 ] ) <= 0.2 * 255.0 && std::max( c1[ 0 ], c2[ 0 ] ) >= 250.0;
		Check( missing && status.opened[ 0 ] == '\0' && g_opens.load() == opensBefore && blinks,
		       fmt( "(e) a saved key with no device: %s, nothing opened in its place (opened \"%s\"), the lamp blinking red (R %.0f then %.0f)",
		            earshot::toString( state() ), status.opened, c1[ 0 ], c2[ 0 ] ) );
	}
	//(f) Rescan with the device back reopens it by name.
	{
		std::lock_guard< std::mutex > lock( g_deviceMutex );
		g_deviceNames.push_back( "Gone Mic" );
	}
	press( PT_RESCAN );
	{
		bool listed = false;
		for( unsigned int e = 0; e < rig.plugin.GetNumParamElements( PT_INPUT ); ++e )
			listed = listed || ( std::string( rig.plugin.GetParamElementName( PT_INPUT, e ) ) == "Gone Mic" && elementValue( rig.plugin, PT_INPUT, e ) == goneKey );
		earshot::Status status;
		const bool running = waitFor(
			[ & ] {
				rig.plugin.AudioForTest().Engine().status( status );
				return state() == earshot::CaptureState::Running && std::string( status.opened ) == "Gone Mic";
			},
			3.0 );
		Check( listed && running, fmt( "(f) Rescan with Gone Mic connected: listed with its key (%s), reopened by name (%s, opened \"%s\")",
		                               listed ? "yes" : "no", earshot::toString( state() ), status.opened ) );
	}
	rig.Set( PT_INPUT, 0.0f );
	rig.Render( 1 );
	{
		std::lock_guard< std::mutex > lock( g_deviceMutex );
		g_deviceNames = { "Test Mic", "Test Silent", "Test Slow" };
	}
	if( rig.lost > 0 )
		Note( fmt( "%llu tap samples arrived in frames bigger than the trace (the device path takes all that queued); the model checked each one",
		           static_cast< unsigned long long >( rig.lost ) ) );
	return g_failures == before ? 0 : 1;
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< runs without a GL context (its picture half skipped)
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "smallsignal", runSmallSignal, true }, { "harmonics", runHarmonics, true }, { "xmech", runXmech, true },
		{ "framerate", runFramerate, true },     { "pumping", runPumping, true },     { "flattop", runFlattop, true },
		{ "drift", runDrift, true },             { "surround", runSurround, true },   { "rest", runRest, false },
		{ "magnify", runMagnify, false },        { "alpha", runAlpha, false },        { "resize", runResize, false },
		{ "fallback", runFallback, false },      { "hostbins", runHostFft, true },     { "devices", runDevices, false },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		HarnessPerturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( HarnessPerturb& ) > set ) {
		HarnessPerturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "smallsignal", runSmallSignal, "the box's force dropped (noBox)", []( HarnessPerturb& p ) { p.model.noBox = true; } );
	add( "smallsignal", runSmallSignal, "the coil's inductance dropped (noLe)", []( HarnessPerturb& p ) { p.model.noLe = true; } );
	add( "smallsignal", runSmallSignal, "back-EMF with the wrong sign", []( HarnessPerturb& p ) { p.model.backEmfSign = true; } );
	add( "harmonics", runHarmonics, "Bl a parabola: no plateau", []( HarnessPerturb& p ) { p.model.blParabolic = true; } );
	add( "harmonics", runHarmonics, "the air spring linearised", []( HarnessPerturb& p ) { p.model.linearBox = true; } );
	add( "xmech", runXmech, "no stop at Xmech", []( HarnessPerturb& p ) { p.model.noStop = true; } );
	add( "framerate", runFramerate, "one step a frame on the frame's mean voltage", []( HarnessPerturb& p ) { p.model.frameStep = true; } );
	add( "pumping", runPumping, "a light cone: Mms x 1e-2, fc ten times higher", []( HarnessPerturb& p ) { p.model.lightCone = true; } );
	add( "flattop", runFlattop, "Bl flat and the suspension linear", []( HarnessPerturb& p ) {
		p.model.blConstant = true;
		p.model.suspLinear = true;
	} );
	add( "drift", runDrift, "Bl symmetric about rest whatever the offset", []( HarnessPerturb& p ) { p.model.ignoreOffset = true; } );
	add( "drift", runDrift, "the air spring linearised", []( HarnessPerturb& p ) { p.model.linearBox = true; } );
	add( "surround", runSurround, "the roll stretched (a semicircle on its chord)", []( HarnessPerturb& p ) { p.profile.stretchRoll = true; } );
	add( "rest", runRest, "painted by material radius, ignoring the perspective", []( HarnessPerturb& p ) { p.profile.printMaterial = true; } );
	add( "magnify", runMagnify, "an orthographic camera", []( HarnessPerturb& p ) { p.profile.ortho = true; } );
	add( "alpha", runAlpha, "the clip's alpha passed through", []( HarnessPerturb& p ) { p.hooks = shaders::kHookAlphaPassthrough; } );
	add( "resize", runResize, "a new raster resets the driver", []( HarnessPerturb& p ) { p.resetOnResize = true; } );
	add( "fallback", runFallback, "the fallback reads the (empty) tap", []( HarnessPerturb& p ) { p.fallbackFromTap = true; } );
	add( "fallback", runFallback, "a millisecond host clock read as seconds", []( HarnessPerturb& p ) { p.clockAsSeconds = true; } );
	add( "hostbins", runHostFft, "Resolume's bins filed whole under the band holding each one's centre (earshot 8124191's rule)",
	     []( HarnessPerturb& p ) { p.hostCentreOnly = true; } );
	add( "hostbins", runHostFft, "the host's sample rate never reaches earshot", []( HarnessPerturb& p ) { p.ignoreHostRate = true; } );
	add( "hostbins", runHostFft, "a host bin's whole energy to every band it touches, not shared", []( HarnessPerturb& p ) { p.hostWholeBin = true; } );
	add( "devices", runDevices, "menu values are positions, not keys", []( HarnessPerturb& p ) { p.menuByIndex = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );
	//WFTEST_NEGATIVE=surround: only that check's wrong models (while debugging one).
	if( const char* only = std::getenv( "WFTEST_NEGATIVE" ) )
		cases.erase( std::remove_if( cases.begin(), cases.end(), [ & ]( const Case& c ) { return std::string( c.name ) != only; } ), cases.end() );

	int unfalsifiable    = 0;
	const bool wasQuiet  = g_quiet;
	g_negative           = true;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		//Quiet, except while debugging one check's wrong models.
		g_quiet          = std::getenv( "WFTEST_NEGATIVE" ) == nullptr;
		c.check( c.perturb );
		g_quiet            = wasQuiet;
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed against it, as it must\n", c.name );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	g_negative = false;
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( HarnessPerturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the physics, with no GL context. The picture checks and halves were NOT run --\n"
	             "  tools/verify.sh runs them on a real driver at 1280x720 and 320x180, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED, and the CPU's share.
//===========================================================================
double median( std::vector< double > v )
{
	if( v.empty() )
		return 0.0;
	std::sort( v.begin(), v.end() );
	return v[ v.size() / 2 ];
}

int runBench( bool with4k )
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	std::vector< Size > sizes = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" } };
	if( with4k )
		sizes.push_back( { 3840, 2160, "4K" } );
	std::printf( "\n=== bench: the defaults on the harness's track; GPU time per frame (GL_TIME_ELAPSED) and the CPU's share,\n"
	             "    after 120 frames of warm-up, median of 120 (4K: of 30, once -- the machine is shared)\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Size& size : sizes )
	{
		Rig rig;
		if( !rig.Init( size.w, size.h ) )
			return 1;
		rig.feeder.signal = mono( musicAt );
		if( !rig.Render( size.w > 2000 ? 30 : 120 ) )
			return 1;
		const int timed = size.w > 2000 ? 30 : 120;
		std::vector< double > gpu, wall, integrate, table, audio, samples;
		for( int f = 0; f < timed; ++f )
		{
			const auto start = std::chrono::steady_clock::now();
			glBeginQuery( GL_TIME_ELAPSED, query );
			if( !rig.Render( 1 ) )
				return 1;
			glEndQuery( GL_TIME_ELAPSED );
			GLuint64 ns = 0;
			glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
			gpu.push_back( static_cast< double >( ns ) * 1e-6 );
			wall.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
			integrate.push_back( rig.plugin.IntegrateMicros() );
			table.push_back( rig.plugin.TableMicros() );
			audio.push_back( rig.plugin.AudioMicros() );
			samples.push_back( rig.plugin.SamplesLastFrame() );
		}
		std::printf( "  %-6s GPU %6.2f ms (%4.1f %% of a 60 fps frame); wall %6.2f ms; CPU: integration %6.1f us for %.0f samples "
		             "(%.2f us each), table %6.1f us (%d rows), earshot %6.1f us\n",
		             size.name, median( gpu ), 100.0 * median( gpu ) / ( 1000.0 / 60.0 ), median( wall ), median( integrate ), median( samples ),
		             median( integrate ) / std::max( 1.0, median( samples ) ), median( table ), rig.plugin.RowsLastFrame(), median( audio ) );
	}
	glDeleteQueries( 1, &query );
	return 0;
}
