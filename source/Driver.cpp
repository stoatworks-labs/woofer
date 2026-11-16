#include "Driver.h"

#include <algorithm>
#include <cmath>

namespace woofer
{
namespace
{
/// Stiffening past Xsus: Kms rises to (1 + kStiffen) Kms at Xmech. Assumed:
/// no datasheet in the set gives Kms(x) (SPEC-woofer.md, "Suspension").
constexpr double kStiffen = 3.0;
/// The stop's coefficient of restitution. Assumed.
constexpr double kRestitution = 0.3;
/// An impact slower than this (m/s) is resting contact, not a hit.
constexpr double kHitSpeed = 1e-3;

//---------------------------------------------------------------------------
// The presets. Primary values exactly as printed; see SPEC-woofer.md for the
// recomputed Fs/Q/Vas against the printed ones.
//---------------------------------------------------------------------------
Preset bcRow()
{
	// B&C Speakers 18SW115-16, datasheet "Rev: 0", 10/06/2011 (distributor's
	// mirror; read 2026-11-16). Fs 35, Qms 7.21, Qes 0.48, Qts 0.45, Re 12.8,
	// Le 3.18 mH, Bl 39.37, Mms 263.8 g, Cms 79.7 um/N, Sd 1210 cm^2, VC 116
	// mm, winding depth 34, gap depth 12, Xmax 14.0 (= (34-12)/2 + 12/4).
	// No Rms: derived from the printed Qms. No D: radius sqrt(Sd/pi). No
	// Xmech: ASSUMED (Hvc + Hg)/2 = 23 mm.
	Preset p    = { "18 in PA Sub", "B&C 18SW115-16", 12.8, 3.18e-3, 39.37, 263.8e-3, 79.7e-6, 0.0, 1210e-4, 0.0, 0.230,
	                0.058, 34e-3, 12e-3, 23e-3, 1250.0, 3.0 };
	const double ws = 1.0 / std::sqrt( p.Mms * p.Cms );
	p.Rms           = ws * p.Mms / 7.21;
	p.coneRadius    = std::sqrt( p.Sd / kPi );
	return p;
}

const Preset* table()
{
	static const Preset rows[ kPresetCount ] = {
		// Scan-Speak 10F/8414G10 Discovery, datasheet "updated May 18, 2020".
		// Overall 97.5 mm; VC 20 mm, 8.7 mm high; gap 4 mm; +-2.3 / +-7.5 mm.
		{ "10 cm Full-range", "Scan-Speak 10F/8414G10", 6.3, 0.11e-3, 4.27, 2.93e-3, 0.86e-3, 0.76, 36e-4, 0.034, 0.04875,
		  0.010, 8.7e-3, 4e-3, 7.5e-3, 10000.0, 3.0 },
		// Scan-Speak 18W/8531G00 Revelator, "updated January 7, 2021".
		// Overall 182.2 mm; VC 38 mm, 18 mm high; gap 5 mm; +-6.5 / +-11 mm.
		{ "18 cm Midwoofer", "Scan-Speak 18W/8531G00", 5.8, 0.35e-3, 6.8, 17.5e-3, 1.85e-3, 0.60, 157e-4, 0.069, 0.0911,
		  0.019, 18e-3, 5e-3, 11e-3, 5000.0, 1.5 },
		// Scan-Speak 26W/8861T00 Revelator, "updated January 7, 2021".
		// Overall 260 mm; VC 50 mm, 24 mm high; gap 6 mm; +-9 / +-14 mm.
		{ "26 cm Woofer", "Scan-Speak 26W/8861T00", 6.2, 0.35e-3, 9.9, 43.5e-3, 1.61e-3, 1.00, 320e-4, 0.101, 0.130, 0.025,
		  24e-3, 6e-3, 14e-3, 3150.0, 3.0 },
		// Scan-Speak 30W/4558T00 Discovery, "updated December 7, 2022".
		// Overall 308.3 mm; VC 51 mm, 33 mm high; gap 8 mm; +-12.5 / +-28 mm.
		{ "30 cm Subwoofer", "Scan-Speak 30W/4558T00", 2.6, 0.83e-3, 10.5, 135e-3, 0.65e-3, 2.88, 466e-4, 0.122, 0.15415,
		  0.0255, 33e-3, 8e-3, 28e-3, 4000.0, 8.0 },
		bcRow(),
	};
	return rows;
}

/// exp( M ) for a 3x3 matrix: scaling and squaring around a Taylor series.
void expm3( const double m[ 3 ][ 3 ], double out[ 3 ][ 3 ] )
{
	double norm = 0.0;
	for( int r = 0; r < 3; ++r )
	{
		double row = 0.0;
		for( int c = 0; c < 3; ++c )
			row += std::fabs( m[ r ][ c ] );
		norm = std::max( norm, row );
	}
	int squarings = 0;
	while( norm > 0.25 )
	{
		norm *= 0.5;
		++squarings;
	}
	const double scale = std::ldexp( 1.0, -squarings );
	double a[ 3 ][ 3 ], term[ 3 ][ 3 ], sum[ 3 ][ 3 ];
	for( int r = 0; r < 3; ++r )
		for( int c = 0; c < 3; ++c )
		{
			a[ r ][ c ]    = m[ r ][ c ] * scale;
			term[ r ][ c ] = r == c ? 1.0 : 0.0;
			sum[ r ][ c ]  = term[ r ][ c ];
		}
	for( int k = 1; k <= 18; ++k )
	{
		double next[ 3 ][ 3 ] = {};
		for( int r = 0; r < 3; ++r )
			for( int c = 0; c < 3; ++c )
			{
				for( int j = 0; j < 3; ++j )
					next[ r ][ c ] += term[ r ][ j ] * a[ j ][ c ];
				next[ r ][ c ] /= k;
			}
		for( int r = 0; r < 3; ++r )
			for( int c = 0; c < 3; ++c )
			{
				term[ r ][ c ] = next[ r ][ c ];
				sum[ r ][ c ] += term[ r ][ c ];
			}
	}
	for( int s = 0; s < squarings; ++s )
	{
		double next[ 3 ][ 3 ] = {};
		for( int r = 0; r < 3; ++r )
			for( int c = 0; c < 3; ++c )
				for( int j = 0; j < 3; ++j )
					next[ r ][ c ] += sum[ r ][ j ] * sum[ j ][ c ];
		for( int r = 0; r < 3; ++r )
			for( int c = 0; c < 3; ++c )
				sum[ r ][ c ] = next[ r ][ c ];
	}
	for( int r = 0; r < 3; ++r )
		for( int c = 0; c < 3; ++c )
			out[ r ][ c ] = sum[ r ][ c ];
}

/// The overlap of the coil window [c - H/2, c + H/2] with the gap's field: a
/// trapezoid in z, flat over |z| <= Hg/4 and falling linearly to zero at
/// 3 Hg/4, so its area is Hg (the "Hg/4 fringe" convention B&C's Xmax obeys).
/// Closed form: the field's running integral is piecewise quadratic.
double coilOverlap( double c, double H, double Hg )
{
	const double a1 = 0.25 * Hg, a2 = 0.75 * Hg, ramp = a2 - a1;
	auto W          = [ & ]( double z ) {
        if( z <= -a2 )
            return 0.0;
        if( z <= -a1 )
            return ( z + a2 ) * ( z + a2 ) / ( 2.0 * ramp );
        if( z <= a1 )
            return 0.5 * ramp + ( z + a1 );
        if( z <= a2 )
            return ramp + 2.0 * a1 - ( a2 - z ) * ( a2 - z ) / ( 2.0 * ramp );
        return a1 + a2;
	};
	return W( c + 0.5 * H ) - W( c - 0.5 * H );
}

/// Catmull-Rom through p1 (u = 0) and p2 (u = 1).
double catmullRom( double p0, double p1, double p2, double p3, double u )
{
	return 0.5 * ( 2.0 * p1 + ( p2 - p0 ) * u + ( 2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 ) * u * u
	               + ( 3.0 * ( p1 - p2 ) + p3 - p0 ) * u * u * u );
}
} // namespace

const Preset& PresetAt( int index )
{
	return table()[ std::clamp( index, 0, kPresetCount - 1 ) ];
}

Derived Derive( const Preset& p )
{
	Derived d;
	const double ws = 1.0 / std::sqrt( p.Mms * p.Cms );
	d.fs            = ws / ( 2.0 * kPi );
	d.Qes           = ws * p.Mms * p.Re / ( p.Bl * p.Bl );
	d.Qms           = ws * p.Mms / p.Rms;
	d.Qts           = 1.0 / ( 1.0 / d.Qes + 1.0 / d.Qms );
	d.Vas           = kRhoC2 * p.Sd * p.Sd * p.Cms;
	d.Xsus          = 0.5 * ( p.Hvc - p.Hg );
	return d;
}

double DriverModel::ReferenceVolts( const Preset& p )
{
	const Derived d    = Derive( p );
	const double a0707 = std::max( 0.0, ( 0.707 / d.Qts ) * ( 0.707 / d.Qts ) - 1.0 );
	return d.Xsus * p.Re * ( 1.0 + a0707 ) / ( p.Bl * p.Cms );
}

//---------------------------------------------------------------------------
DriverModel::DriverModel() :
	trace( kTraceLength )
{
	Configure( DriverSettings() );
}

void DriverModel::Configure( const DriverSettings& s )
{
	settings        = s;
	preset          = &PresetAt( s.preset );
	const Preset& p = *preset;
	const Derived d = Derive( p );

	hvc   = p.Hg + std::max( 0.0, s.xmaxScale ) * ( p.Hvc - p.Hg );
	x0    = s.offsetFraction * p.Hg;
	alpha = s.qtc > d.Qts ? ( s.qtc / d.Qts ) * ( s.qtc / d.Qts ) - 1.0 : 0.0;
	vb    = alpha > 0.0 ? d.Vas / alpha : 0.0;
	mms   = p.Mms * ( perturb.lightCone ? 1e-2 : 1.0 );

	//Bl is the printed value at rest, wherever rest sits in the gap.
	const double overlap = coilOverlap( perturb.ignoreOffset ? 0.0 : x0, hvc, p.Hg );
	blNorm               = overlap > 0.0 ? 1.0 / overlap : 0.0;

	//The modes: f_n = f_b (2n - 1), Gamma_n = 1/n, below the tap's passband.
	modeCount       = 0;
	const double h  = 1.0 / kTapRate;
	const double fb = p.breakupHz * std::max( 0.0, s.breakupScale );
	for( int n = 0; n < kModes; ++n )
	{
		const double f = fb * ( 2.0 * n + 1.0 );
		if( !( f > 0.0 ) || f > 0.4 * kTapRate )
			break;
		const double w = 2.0 * kPi * f, gamma = 1.0 / ( n + 1.0 );
		const double m[ 3 ][ 3 ] = { { 0.0, h, 0.0 }, { -w * w * h, -w / p.breakupQ * h, -gamma * h }, { 0.0, 0.0, 0.0 } };
		double e[ 3 ][ 3 ];
		expm3( m, e );
		phi[ n ][ 0 ]  = e[ 0 ][ 0 ];
		phi[ n ][ 1 ]  = e[ 0 ][ 1 ];
		phi[ n ][ 2 ]  = e[ 1 ][ 0 ];
		phi[ n ][ 3 ]  = e[ 1 ][ 1 ];
		gam[ n ][ 0 ]  = e[ 0 ][ 2 ];
		gam[ n ][ 1 ]  = e[ 1 ][ 2 ];
		modeHz[ n ]    = f;
		modeGamma[ n ] = gamma;
		++modeCount;
	}
	for( int n = modeCount; n < kModes; ++n )
		q[ n ][ 0 ] = q[ n ][ 1 ] = 0.0;
}

void DriverModel::Reset()
{
	state = { 0.0, 0.0, 0.0 };
	for( auto& m : q )
		m[ 0 ] = m[ 1 ] = 0.0;
	std::fill( std::begin( history ), std::end( history ), 0.0 );
	std::fill( trace.begin(), trace.end(), Sample() );
	count    = 0;
	maxAbsX  = 0.0;
	stopHits = 0;
	lastHit  = 0;
}

//---------------------------------------------------------------------------
double DriverModel::Bl( double x ) const
{
	const Preset& p = *preset;
	if( perturb.blConstant )
		return p.Bl;
	const double c = x + ( perturb.ignoreOffset ? 0.0 : x0 );
	if( perturb.blParabolic )
	{
		const double r = c / ( 2.0 * Xlin() );
		return p.Bl * std::max( 0.05, 1.0 - r * r );
	}
	return p.Bl * coilOverlap( c, hvc, p.Hg ) * blNorm;
}

double DriverModel::Fsus( double x ) const
{
	const Preset& p   = *preset;
	const double kms  = 1.0 / p.Cms;
	if( perturb.suspLinear )
		return kms * x;
	const double xsus = 0.5 * ( p.Hvc - p.Hg );
	const double span = p.Xmech - xsus;
	const double u    = std::max( 0.0, std::fabs( x ) - xsus ) / span;
	return kms * ( x + std::copysign( kStiffen * span * u * u * u / 3.0, x ) );
}

double DriverModel::Fbox( double x ) const
{
	if( alpha <= 0.0 || perturb.noBox )
		return 0.0;
	if( perturb.linearBox )
		return -Kbox() * x;
	const Preset& p = *preset;
	//Adiabatic: P V^gamma constant. Linearised, -(rho c^2 Sd^2 / Vb) x.
	return kP0 * p.Sd * ( std::exp( -kGamma * std::log1p( p.Sd * x / vb ) ) - 1.0 );
}

DriverModel::State DriverModel::Derivative( const State& s, double volts ) const
{
	const Preset& p   = *preset;
	const double bl   = Bl( s.x );
	const double emf  = ( perturb.backEmfSign ? -1.0 : 1.0 ) * bl * s.v;
	const double i    = perturb.noLe ? ( volts - emf ) / p.Re : s.i;
	State d;
	d.x = s.v;
	d.v = ( bl * i - p.Rms * s.v - Fsus( s.x ) + Fbox( s.x ) ) / mms;
	d.i = perturb.noLe ? 0.0 : ( volts - p.Re * s.i - emf ) / p.Le;
	return d;
}

/// One tap interval, from sample k to k + 1, with v0..v3 the samples k - 1
/// .. k + 2.
void DriverModel::Step( double v0, double v1, double v2, double v3 )
{
	const Preset& p   = *preset;
	const double h    = 1.0 / ( static_cast< double >( kTapRate ) * kSubsteps );
	const double vOld = state.v;
	for( int j = 0; j < kSubsteps; ++j )
	{
		const double u0 = static_cast< double >( j ) / kSubsteps;
		const double du = 1.0 / kSubsteps;
		const double va = catmullRom( v0, v1, v2, v3, u0 );
		const double vm = catmullRom( v0, v1, v2, v3, u0 + 0.5 * du );
		const double vEnd = catmullRom( v0, v1, v2, v3, u0 + du );
		const State k1  = Derivative( state, va );
		const State s2  = { state.x + 0.5 * h * k1.x, state.v + 0.5 * h * k1.v, state.i + 0.5 * h * k1.i };
		const State k2  = Derivative( s2, vm );
		const State s3  = { state.x + 0.5 * h * k2.x, state.v + 0.5 * h * k2.v, state.i + 0.5 * h * k2.i };
		const State k3  = Derivative( s3, vm );
		const State s4  = { state.x + h * k3.x, state.v + h * k3.v, state.i + h * k3.i };
		const State k4  = Derivative( s4, vEnd );
		state.x += h / 6.0 * ( k1.x + 2.0 * k2.x + 2.0 * k3.x + k4.x );
		state.v += h / 6.0 * ( k1.v + 2.0 * k2.v + 2.0 * k3.v + k4.v );
		state.i += h / 6.0 * ( k1.i + 2.0 * k2.i + 2.0 * k3.i + k4.i );
		if( perturb.noLe )
			state.i = ( vEnd - ( perturb.backEmfSign ? -1.0 : 1.0 ) * Bl( state.x ) * state.v ) / p.Re;

		//The stop: the coil cannot pass Xmech.
		if( !perturb.noStop && std::fabs( state.x ) > p.Xmech )
		{
			const double side = state.x > 0.0 ? 1.0 : -1.0;
			state.x           = side * p.Xmech;
			if( state.v * side > 0.0 )
			{
				if( state.v * side > kHitSpeed )
				{
					++stopHits;
					lastHit = count;
				}
				state.v = -kRestitution * state.v;
			}
		}
		maxAbsX = std::max( maxAbsX, std::fabs( state.x ) );
	}

	//The modes, on the interval's mean acceleration (held over the sample).
	const double a = ( state.v - vOld ) * kTapRate;
	for( int n = 0; n < modeCount; ++n )
	{
		const double q0 = q[ n ][ 0 ], q1 = q[ n ][ 1 ];
		q[ n ][ 0 ]     = phi[ n ][ 0 ] * q0 + phi[ n ][ 1 ] * q1 + gam[ n ][ 0 ] * a;
		q[ n ][ 1 ]     = phi[ n ][ 2 ] * q0 + phi[ n ][ 3 ] * q1 + gam[ n ][ 1 ] * a;
	}
}

void DriverModel::Push()
{
	Sample& s = trace[ count % kTraceLength ];
	s.x       = state.x;
	s.v       = state.v;
	s.i       = state.i;
	for( int n = 0; n < kModes; ++n )
		s.q[ n ] = q[ n ][ 0 ];
	++count;
}

void DriverModel::Run( const float* volts, int n )
{
	if( n <= 0 || volts == nullptr )
		return;
	double mean = 0.0;
	if( perturb.frameStep )
	{
		for( int k = 0; k < n; ++k )
			mean += volts[ k ];
		mean /= n;
	}
	for( int k = 0; k < n; ++k )
	{
		history[ 0 ] = history[ 1 ];
		history[ 1 ] = history[ 2 ];
		history[ 2 ] = history[ 3 ];
		history[ 3 ] = perturb.frameStep ? mean : static_cast< double >( volts[ k ] );
		//Integrate the interval between the middle two: one sample behind.
		Step( history[ 0 ], history[ 1 ], history[ 2 ], history[ 3 ] );
		Push();
	}
}

Sample DriverModel::At( double back ) const
{
	if( count == 0 )
		return Sample();
	const double held = static_cast< double >( std::min< uint64_t >( count, kTraceLength ) - 1 );
	back              = std::clamp( back, 0.0, held );
	const double k    = std::floor( back );
	const double f    = back - k;
	const uint64_t i0 = count - 1 - static_cast< uint64_t >( k );
	const uint64_t i1 = f > 0.0 ? i0 - 1 : i0;
	const Sample& a   = trace[ i0 % kTraceLength ];
	const Sample& b   = trace[ i1 % kTraceLength ];
	Sample s;
	s.x = a.x + ( b.x - a.x ) * f;
	s.v = a.v + ( b.v - a.v ) * f;
	s.i = a.i + ( b.i - a.i ) * f;
	for( int n = 0; n < kModes; ++n )
		s.q[ n ] = a.q[ n ] + ( b.q[ n ] - a.q[ n ] ) * f;
	return s;
}

} // namespace woofer
