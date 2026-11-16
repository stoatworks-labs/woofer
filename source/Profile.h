#pragma once

#include "Driver.h"

#include <vector>

/**
    The driver's front as the camera sees it: an axisymmetric profile of
    material points (dust cap, cone, surround, flange), moved by the model's
    state, projected through a pinhole on the axis, and rasterised into a 1-D
    z-buffered table of screen radius that the shader looks every pixel up in.

    **Printing.** The clip is projected onto the driver at rest from the
    camera's own position and the paint stays on the material, so the rest
    view is exactly the clip. Material the projection could not see at rest
    (the roll's outer foot behind its crest) is unpainted; it shows the bare
    surround if a stroke brings it into view.

    **Geometry** (assumed; datasheets give overall and coil diameters only):
    the cone straight at 25 degrees from the dust cap to its rim, flush with
    the flange; a dust cap 0.6 x the coil diameter in radius, a spherical
    dome 0.35 of that high; a half-roll surround of width Xmech whose arc
    length is conserved (it is the circular arc of that length through its two
    moving ends); a flat flange to the overall diameter.

    **Units**: metres for the driver; the screen in frame heights, centred.

    **A table bin** is 8 floats, two texels: (rest screen radius, normal r,
    normal z, paint) and (bare reflectance, gloss, axial displacement, 0).
*/
namespace woofer
{

struct ProfileSettings
{
	double size   = 0.95;///< overall diameter, x frame height, at the baffle
	double camera = 1.5; ///< camera distance, x overall diameter
	double ripple = 150.0;

	// Negative controls (wftest --negative).
	bool ortho         = false;///< projection without perspective
	bool printMaterial = false;///< paint by material radius, ignoring the rest projection
	bool stretchRoll   = false;///< the roll a semicircle on its chord: stretched, not bent
};

enum class Part : int
{
	DustCap = 0,
	Cone,
	Surround,
	Flange
};

class Profile
{
public:
	static constexpr int kPoints = 2048;
	static constexpr int kBins   = 1024;
	static constexpr int kRows   = 16;
	static constexpr int kFloatsPerBin = 8;

	Profile();

	/// Rebuild the rest geometry and the paint. Cheap; call when the preset,
	/// Size or Camera changes.
	void Configure( const DriverModel& model, const ProfileSettings& settings );

	/// One table row for the state `s`: kBins x 4 floats into each of `a`, `b`.
	void Row( const DriverModel& model, const Sample& s, float* a, float* b );

	/// Every point's current position for `s` (r, z), for the harness.
	void Positions( const DriverModel& model, const Sample& s, std::vector< double >& r, std::vector< double >& z ) const;

	double RhoMax() const
	{
		return rhoMax;
	}
	/// r / (D - z) per frame-height of screen radius: a pixel's ray.
	double RayScale() const
	{
		return 1.0 / kp;
	}
	double CameraDistance() const
	{
		return dc;
	}
	/// Screen radius (frame heights) of a point at (r, z).
	double Project( double r, double z ) const;

	/// The surround's polyline length for `s`, and at rest.
	double SurroundLength( const DriverModel& model, const Sample& s ) const;
	double SurroundLengthAtRest() const
	{
		return restSurroundLength;
	}
	/// Segments painted at rest (for the harness).
	int PaintedCount() const;
	/// Whether material at rest screen radius `rest` and rest height `z` was
	/// reached by the printing.
	bool Painted( double rest, double z ) const;
	Part PartOf( int j ) const
	{
		return part[ j ];
	}
	double RestR( int j ) const
	{
		return r0[ j ];
	}
	double RestZ( int j ) const
	{
		return z0[ j ];
	}
	/// The cone's rest z at radius r (the harness's ring placement).
	double ConeRestZ( double r ) const;
	double ConeInner() const
	{
		return rdc;
	}
	double ConeOuter() const
	{
		return coneR;
	}

private:
	void Place( const DriverModel& model, const Sample& s, double* r, double* z ) const;
	void Arc( double r0_, double z0_, double r1, double z1, int count, double* r, double* z ) const;

	ProfileSettings settings;
	double coneR = 0.1, rvc = 0.02, rdc = 0.03, hdc = 0.01, roll = 0.02, outer = 0.15;
	double dc = 1.0, kp = 1.0, rhoMax = 0.5;
	double restSurroundLength = 0.0;
	int first[ 5 ] = {};///< the first point of each part, and kPoints

	std::vector< double > r0, z0, s0, rho0, restRho;
	std::vector< Part > part;
	std::vector< double > shape;///< kModes x kPoints: phi_n at each point
	double rimShape[ DriverModel::kModes ] = {};

	// Scratch, allocated once.
	std::vector< double > cr, cz, cd, crho, nr, nz;
	std::vector< double > depth;
	std::vector< double > restDepth;
};

} // namespace woofer
