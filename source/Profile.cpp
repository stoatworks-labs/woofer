#include "Profile.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace woofer
{
namespace
{
constexpr double kConeAngle = 25.0 * kPi / 180.0;///< assumed: a straight cone at 25 degrees
constexpr double kCapRadius = 0.6;               ///< x coil diameter (assumed)
constexpr double kCapHeight = 0.35;              ///< x cap radius (assumed)

/// How far behind the rest surface (x the roll's width) material must be to
/// count as unreached by the printing.
constexpr double kPaintSlack = 0.05;

/// The Ripple gain exaggerates the breakup modes for the eye; their drawn
/// displacement is soft-limited (tanh) to this fraction of the roll's width
/// (= Xmech). A stop impact rings the modes millimetres hard, and x 150 that
/// would lift the cone's rim out of the surround's reach: with the rim at
/// most 1.15 Xmech from rest, the roll's chord is at most 1.52 of its width,
/// under the arc's length (pi/2 = 1.571), so the roll always bends and never
/// has to stretch (wftest --surround).
constexpr double kRippleLimit = 0.15;

constexpr int kCapPoints      = 192;
constexpr int kConePoints     = 896;
constexpr int kSurroundPoints = 768;
constexpr int kFlangePoints   = Profile::kPoints - kCapPoints - kConePoints - kSurroundPoints;

/// Bare reflectance (linear) and whether the part is glossy, by Part.
constexpr double kBase[ 4 ]  = { 0.06, 0.04, 0.02, 0.30 };
constexpr double kGloss[ 4 ] = { 1.0, 1.0, 1.0, 0.0 };

/// sin(t)/t = k for t in (0, pi): Newton, bracketed by bisection.
double solveSinc( double k )
{
	if( k >= 1.0 )
		return 0.0;
	if( k <= 0.0 )
		return kPi;
	double lo = 0.0, hi = kPi;
	double t  = std::clamp( std::sqrt( 6.0 * ( 1.0 - k ) ), 1e-6, kPi - 1e-6 );
	for( int it = 0; it < 100; ++it )
	{
		const double f = std::sin( t ) - k * t;// > 0 below the root
		if( f > 0.0 )
			lo = t;
		else
			hi = t;
		const double df = std::cos( t ) - k;
		double next     = df != 0.0 ? t - f / df : 0.5 * ( lo + hi );
		if( !( next > lo && next < hi ) )
			next = 0.5 * ( lo + hi );
		if( std::fabs( next - t ) < 1e-15 )
		{
			t = next;
			break;
		}
		t = next;
	}
	return t;
}
} // namespace

Profile::Profile() :
	r0( kPoints ), z0( kPoints ), s0( kPoints ), rho0( kPoints ), restRho( kPoints ), part( kPoints ),
	shape( static_cast< size_t >( DriverModel::kModes ) * kPoints ), cr( kPoints ),
	cz( kPoints ), cd( kPoints ), crho( kPoints ), nr( kPoints ), nz( kPoints ), depth( kBins ), restDepth( 4 * kBins )
{
	first[ 0 ] = 0;
	first[ 1 ] = kCapPoints;
	first[ 2 ] = kCapPoints + kConePoints;
	first[ 3 ] = kCapPoints + kConePoints + kSurroundPoints;
	first[ 4 ] = kPoints;
	static_assert( kFlangePoints > 1, "the flange needs points" );
}

double Profile::ConeRestZ( double r ) const
{
	return -( coneR - r ) * std::tan( kConeAngle );
}

double Profile::Project( double r, double z ) const
{
	if( settings.ortho )
		return 0.5 * settings.size * r / outer;
	return kp * r / ( dc - z );
}

void Profile::Arc( double ra, double za, double rb, double zb, int count, double* r, double* z ) const
{
	//A circular arc of the roll's own length through both ends, bulging to
	//the left of the chord (outward at rest), its points at equal fractions of
	//arc length: the material does not stretch.
	const double length = 0.5 * kPi * roll;
	const double dr = rb - ra, dz = zb - za;
	const double chord = std::sqrt( dr * dr + dz * dz );
	double theta       = settings.stretchRoll ? 0.5 * kPi : solveSinc( chord / length );
	if( chord <= 0.0 )
		theta = kPi;
	if( theta < 1e-9 )
	{
		for( int k = 0; k < count; ++k )
		{
			const double u = static_cast< double >( k ) / ( count - 1 );
			r[ k ]         = ra + dr * u;
			z[ k ]         = za + dz * u;
		}
		return;
	}
	const double radius = settings.stretchRoll ? 0.5 * chord : length / ( 2.0 * theta );
	const double ur = dr / chord, uz = dz / chord;
	const double nR = -uz, nZ = ur;//left of the chord
	const double mr = 0.5 * ( ra + rb ), mz = 0.5 * ( za + zb );
	const double cr_ = mr - nR * radius * std::cos( theta ), cz_ = mz - nZ * radius * std::cos( theta );
	const double sr = ra - cr_, sz = za - cz_;
	for( int k = 0; k < count; ++k )
	{
		const double u  = static_cast< double >( k ) / ( count - 1 );
		const double a  = -2.0 * theta * u;// clockwise
		const double ca = std::cos( a ), sa = std::sin( a );
		r[ k ]          = cr_ + ca * sr - sa * sz;
		z[ k ]          = cz_ + sa * sr + ca * sz;
	}
	//The ends exactly where they were asked to be.
	r[ 0 ]         = ra;
	z[ 0 ]         = za;
	r[ count - 1 ] = rb;
	z[ count - 1 ] = zb;
}

void Profile::Configure( const DriverModel& model, const ProfileSettings& s )
{
	settings        = s;
	const Preset& p = model.Driver();
	coneR           = p.coneRadius;
	rvc             = p.vcRadius;
	rdc             = kCapRadius * 2.0 * p.vcRadius;
	hdc             = kCapHeight * rdc;
	roll            = p.Xmech;
	outer           = p.overallRadius;
	dc              = s.camera * 2.0 * outer;
	kp              = 0.5 * s.size * dc / outer;
	rhoMax          = 0.5 * s.size;

	//Rest geometry.
	const double zBase  = ConeRestZ( rdc );
	const double sphere = ( rdc * rdc + hdc * hdc ) / ( 2.0 * hdc );
	auto sOf            = [ & ]( double r ) { return std::clamp( ( r - rvc ) / ( coneR - rvc ), 0.0, 1.0 ); };
	for( int j = first[ 0 ]; j < first[ 1 ]; ++j )
	{
		const double r = rdc * ( j - first[ 0 ] ) / ( kCapPoints - 1.0 );
		r0[ j ]        = r;
		z0[ j ]        = zBase + std::sqrt( std::max( 0.0, sphere * sphere - r * r ) ) - ( sphere - hdc );
		s0[ j ]        = sOf( rdc );
		part[ j ]      = Part::DustCap;
	}
	for( int j = first[ 1 ]; j < first[ 2 ]; ++j )
	{
		const double r = rdc + ( coneR - rdc ) * ( j - first[ 1 ] ) / ( kConePoints - 1.0 );
		r0[ j ]        = r;
		z0[ j ]        = ConeRestZ( r );
		s0[ j ]        = sOf( r );
		part[ j ]      = Part::Cone;
	}
	Arc( coneR, 0.0, coneR + roll, 0.0, kSurroundPoints, &r0[ first[ 2 ] ], &z0[ first[ 2 ] ] );
	for( int j = first[ 2 ]; j < first[ 3 ]; ++j )
	{
		s0[ j ]   = 1.0;
		part[ j ] = Part::Surround;
	}
	for( int j = first[ 3 ]; j < first[ 4 ]; ++j )
	{
		r0[ j ]   = coneR + roll + ( outer - coneR - roll ) * ( j - first[ 3 ] ) / ( kFlangePoints - 1.0 );
		z0[ j ]   = 0.0;
		s0[ j ]   = 0.0;
		part[ j ] = Part::Flange;
	}
	//The mode shapes, phi_n( s ) = sin( (n + 1/2) pi s ): fixed per point.
	for( int n = 0; n < DriverModel::kModes; ++n )
	{
		for( int j = 0; j < kPoints; ++j )
		{
			//The dust cap rides on the cone's inner edge as a rigid body.
			const double sc = j < first[ 1 ] ? s0[ first[ 0 ] ] : s0[ j ];
			shape[ static_cast< size_t >( n ) * kPoints + static_cast< size_t >( j ) ] = std::sin( ( n + 0.5 ) * kPi * sc );
		}
		rimShape[ n ] = std::sin( ( n + 0.5 ) * kPi );
	}
	for( int j = 0; j < kPoints; ++j )
	{
		restRho[ j ] = Project( r0[ j ], z0[ j ] );
		rho0[ j ]    = s.printMaterial ? 0.5 * s.size * r0[ j ] / outer : restRho[ j ];
	}

	restSurroundLength = 0.0;
	for( int j = first[ 2 ]; j + 1 < first[ 3 ]; ++j )
		restSurroundLength += std::hypot( r0[ j + 1 ] - r0[ j ], z0[ j + 1 ] - z0[ j ] );

	//The paint: a material point is painted if, at rest, it was the nearest
	//surface at its own screen radius -- the projector at the camera reached
	//it. Kept as the rest z-buffer (4x finer than the table); Row() asks it
	//for each material point it draws.
	const int fine     = static_cast< int >( restDepth.size() );
	const double delta = rhoMax / fine;
	std::fill( restDepth.begin(), restDepth.end(), std::numeric_limits< double >::infinity() );
	for( int j = 0; j + 1 < kPoints; ++j )
	{
		const double ra = restRho[ j ], rb = restRho[ j + 1 ];
		if( ra == rb )
			continue;
		const double lo = std::min( ra, rb ), hi = std::max( ra, rb );
		const int k0 = std::max( 0, static_cast< int >( std::ceil( lo / delta - 0.5 ) ) );
		const int k1 = std::min( fine - 1, static_cast< int >( std::floor( hi / delta - 0.5 ) ) );
		for( int k = k0; k <= k1; ++k )
		{
			const double t = ( ( k + 0.5 ) * delta - ra ) / ( rb - ra );
			const double z = z0[ j ] + ( z0[ j + 1 ] - z0[ j ] ) * t;
			const double d = settings.ortho ? -z : dc - z;
			restDepth[ static_cast< size_t >( k ) ] = std::min( restDepth[ static_cast< size_t >( k ) ], d );
		}
	}
}

bool Profile::Painted( double rest, double z ) const
{
	//Painted unless something else stood in front of it at rest by more than
	//kPaintSlack of the roll: the hidden foot sits behind the crest by about
	//half the roll, while a visible surface's own slope over a bin and a half
	//is under a tenth of the slack at every preset, Size and Camera.
	const int fine     = static_cast< int >( restDepth.size() );
	const int k        = std::clamp( static_cast< int >( std::floor( rest / ( rhoMax / fine ) ) ), 0, fine - 1 );
	double nearest     = restDepth[ static_cast< size_t >( k ) ];
	if( k > 0 )
		nearest = std::min( nearest, restDepth[ static_cast< size_t >( k - 1 ) ] );
	if( k + 1 < fine )
		nearest = std::min( nearest, restDepth[ static_cast< size_t >( k + 1 ) ] );
	const double d = settings.ortho ? -z : dc - z;
	return !( d > nearest + kPaintSlack * roll );
}

void Profile::Place( const DriverModel& model, const Sample& s, double* r, double* z ) const
{
	const int modes    = model.ModeCount();
	const double limit = kRippleLimit * roll;
	auto soft          = [ & ]( double w ) {
        const double drawn = settings.ripple * w;
        return drawn == 0.0 ? 0.0 : limit * std::tanh( drawn / limit );
	};
	auto ripple = [ & ]( int j ) {
		double w = 0.0;
		for( int n = 0; n < modes; ++n )
			w += s.q[ n ] * shape[ static_cast< size_t >( n ) * kPoints + static_cast< size_t >( j ) ];
		return soft( w );
	};
	const double cap = ripple( first[ 0 ] );
	for( int j = first[ 0 ]; j < first[ 1 ]; ++j )
	{
		r[ j ] = r0[ j ];
		z[ j ] = z0[ j ] + s.x + cap;
	}
	for( int j = first[ 1 ]; j < first[ 2 ]; ++j )
	{
		r[ j ] = r0[ j ];
		z[ j ] = z0[ j ] + s.x + ripple( j );
	}
	double rim = 0.0;
	for( int n = 0; n < modes; ++n )
		rim += s.q[ n ] * rimShape[ n ];
	Arc( coneR, s.x + soft( rim ), coneR + roll, 0.0, kSurroundPoints, r + first[ 2 ], z + first[ 2 ] );
	for( int j = first[ 3 ]; j < first[ 4 ]; ++j )
	{
		r[ j ] = r0[ j ];
		z[ j ] = z0[ j ];
	}
}

void Profile::Positions( const DriverModel& model, const Sample& s, std::vector< double >& r, std::vector< double >& z ) const
{
	r.resize( kPoints );
	z.resize( kPoints );
	Place( model, s, r.data(), z.data() );
}

double Profile::SurroundLength( const DriverModel& model, const Sample& s ) const
{
	std::vector< double > r, z;
	Positions( model, s, r, z );
	double length = 0.0;
	for( int j = first[ 2 ]; j + 1 < first[ 3 ]; ++j )
		length += std::hypot( r[ j + 1 ] - r[ j ], z[ j + 1 ] - z[ j ] );
	return length;
}

int Profile::PaintedCount() const
{
	int painted = 0;
	for( int j = 0; j + 1 < kPoints; ++j )
		painted += Painted( 0.5 * ( restRho[ j ] + restRho[ j + 1 ] ), 0.5 * ( z0[ j ] + z0[ j + 1 ] ) ) ? 1 : 0;
	return painted;
}

void Profile::Row( const DriverModel& model, const Sample& s, float* a, float* b )
{
	Place( model, s, cr.data(), cz.data() );
	for( int j = 0; j < kPoints; ++j )
	{
		crho[ j ] = Project( cr[ j ], cz[ j ] );
		cd[ j ]   = settings.ortho ? -cz[ j ] : dc - cz[ j ];
	}
	//Normals from neighbours within each part (a crease between parts stays
	//sharp), pointing out of the front: the tangent turned a quarter left.
	for( int part_ = 0; part_ < 4; ++part_ )
		for( int j = first[ part_ ]; j < first[ part_ + 1 ]; ++j )
		{
			const int lo    = std::max( first[ part_ ], j - 1 );
			const int hi    = std::min( first[ part_ + 1 ] - 1, j + 1 );
			const double tr = cr[ hi ] - cr[ lo ], tz = cz[ hi ] - cz[ lo ];
			const double n  = std::sqrt( tr * tr + tz * tz );
			nr[ j ]         = n > 0.0 ? -tz / n : 0.0;
			nz[ j ]         = n > 0.0 ? tr / n : 1.0;
		}

	const double delta = rhoMax / kBins;
	double* zbuf       = depth.data();
	std::fill( zbuf, zbuf + kBins, std::numeric_limits< double >::infinity() );
	for( int j = 0; j + 1 < kPoints; ++j )
	{
		const double ra = crho[ j ], rb = crho[ j + 1 ];
		if( ra == rb )
			continue;
		const double lo = std::min( ra, rb ), hi = std::max( ra, rb );
		const int k0 = std::max( 0, static_cast< int >( std::ceil( lo / delta - 0.5 ) ) );
		const int k1 = std::min( kBins - 1, static_cast< int >( std::floor( hi / delta - 0.5 ) ) );
		const int pj = static_cast< int >( part[ j ] );
		for( int k = k0; k <= k1; ++k )
		{
			const double t = ( ( k + 0.5 ) * delta - ra ) / ( rb - ra );
			const double d = cd[ j ] + ( cd[ j + 1 ] - cd[ j ] ) * t;
			if( !( d < zbuf[ k ] ) )
				continue;
			zbuf[ k ]  = d;
			float* ta  = a + 4 * k;
			float* tb  = b + 4 * k;
			ta[ 0 ]    = static_cast< float >( rho0[ j ] + ( rho0[ j + 1 ] - rho0[ j ] ) * t );
			ta[ 1 ]    = static_cast< float >( nr[ j ] + ( nr[ j + 1 ] - nr[ j ] ) * t );
			ta[ 2 ]    = static_cast< float >( nz[ j ] + ( nz[ j + 1 ] - nz[ j ] ) * t );
			ta[ 3 ]    = Painted( restRho[ j ] + ( restRho[ j + 1 ] - restRho[ j ] ) * t, z0[ j ] + ( z0[ j + 1 ] - z0[ j ] ) * t ) ? 1.0f : 0.0f;
			const double dispA = cz[ j ] - z0[ j ], dispB = cz[ j + 1 ] - z0[ j + 1 ];
			tb[ 0 ]    = static_cast< float >( kBase[ pj ] );
			tb[ 1 ]    = static_cast< float >( kGloss[ pj ] );
			tb[ 2 ]    = static_cast< float >( dispA + ( dispB - dispA ) * t );
			tb[ 3 ]    = 0.0f;
		}
	}
	//Anything nothing covered is the flat flange (never expected inside).
	for( int k = 0; k < kBins; ++k )
		if( zbuf[ k ] == std::numeric_limits< double >::infinity() )
		{
			float* ta = a + 4 * k;
			float* tb = b + 4 * k;
			ta[ 0 ]   = static_cast< float >( ( k + 0.5 ) * delta );
			ta[ 1 ]   = 0.0f;
			ta[ 2 ]   = 1.0f;
			ta[ 3 ]   = 1.0f;
			tb[ 0 ]   = static_cast< float >( kBase[ 3 ] );
			tb[ 1 ]   = 0.0f;
			tb[ 2 ]   = 0.0f;
			tb[ 3 ]   = 0.0f;
		}
}

} // namespace woofer
