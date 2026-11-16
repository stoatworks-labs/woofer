#pragma once

#include <cstdint>
#include <vector>

/**
    The loudspeaker: five real drivers, and the lumped model that moves them.

    **The presets** are from manufacturers' datasheets, fetched and read on
    2026-11-16; each row below names its document and revision. Everything is
    in SI units. Where a sheet does not print a value the model needs, the row
    says so and says what was assumed (SPEC-woofer.md, "Driver presets").

    **The model** (derived from Newton and Kirchhoff, not cited):

        Le di/dt  = V - Re i - Bl(x) v
        Mms dv/dt = Bl(x) i - Rms v - Fsus(x) + Fbox(x)
        dx/dt     = v

    x is positive OUTWARD (toward the camera). Bl(x) is the overlap of the coil
    window with a trapezoidal gap field, in closed form; Fsus stiffens past
    Xsus; Fbox is the adiabatic air spring of a sealed box set by Qtc; the
    coil stops dead at +-Xmech. Breakup modes are damped oscillators driven by
    the piston's acceleration, discretised exactly.

    **Integrated at the audio rate.** `Run()` takes the tap's samples (32 kHz)
    as volts and integrates through them with RK4 in double, two substeps a
    sample, the voltage between samples from Catmull-Rom (one sample behind).
    Every sample's state goes into a ring the picture is drawn from, so the
    picture depends on the audio and not on the frame rate.

    **Thread**: the render thread only. No allocation after construction.
*/
namespace woofer
{

constexpr double kPi = 3.14159265358979323846;

/// Atmosphere: P0 and gamma; rho c^2 = gamma P0 (adiabatic, ideal gas).
constexpr double kP0    = 101325.0;
constexpr double kGamma = 1.4;
constexpr double kRhoC2 = kGamma * kP0;

struct Preset
{
	const char* option;  ///< the menu's label
	const char* model;   ///< maker and model, for logs and the README
	double Re;           ///< ohm
	double Le;           ///< H
	double Bl;           ///< T m, at rest
	double Mms;          ///< kg
	double Cms;          ///< m/N
	double Rms;          ///< kg/s
	double Sd;           ///< m^2
	double coneRadius;   ///< m, the radius drawn to the surround
	double overallRadius;///< m, the flange's edge
	double vcRadius;     ///< m
	double Hvc;          ///< m, winding height
	double Hg;           ///< m, gap height
	double Xmech;        ///< m, the mechanical limit (assumed for the B&C)
	double breakupHz;    ///< first breakup peak, read off the response plot
	double breakupQ;
};

constexpr int kPresetCount = 5;
const Preset& PresetAt( int index );
/// The default: the 30 cm sub, the longest stroke and the sharpest breakup.
constexpr int kDefaultPreset = 3;

/// Values recomputed from a preset's primary ones (the consistency check
/// against the printed figures, and what the box is set from).
struct Derived
{
	double fs, Qes, Qms, Qts, Vas;
	double Xsus;///< (Hvc - Hg)/2: Scan-Speak's "linear excursion"
};
Derived Derive( const Preset& p );

/// The controls the model reads.
struct DriverSettings
{
	int preset            = kDefaultPreset;
	double qtc            = 0.707;///< <= the driver's Qts: infinite baffle
	double xmaxScale      = 1.0;  ///< scales the coil's overhang
	double offsetFraction = 0.1;  ///< coil rest offset, x gap height
	double breakupScale   = 1.0;  ///< scales every mode's frequency
};

/// The negative controls (wftest --negative). Never set by the plugin itself.
struct Perturb
{
	bool noBox        = false;///< the box's force dropped (Qtc still claimed)
	bool noLe         = false;///< the coil's inductance dropped
	bool backEmfSign  = false;///< back-EMF with the wrong sign
	bool noStop       = false;///< the coil passes Xmech
	bool blParabolic  = false;///< Bl = Bl0 (1 - (c / 2 Xlin)^2): no plateau
	bool blConstant   = false;///< Bl never falls
	bool suspLinear   = false;///< the suspension never stiffens
	bool linearBox    = false;///< the air spring linearised
	bool ignoreOffset = false;///< Bl symmetric about rest whatever the offset
	bool lightCone    = false;///< Mms x 1e-2: fc ten times higher, so the hat is no longer far up the 1/f^2 slope from the kick
	bool frameStep    = false;///< one step per Run() call, on the mean voltage
};

/// One tap sample's state.
struct Sample
{
	double x = 0.0, v = 0.0, i = 0.0;
	double q[ 4 ] = { 0.0, 0.0, 0.0, 0.0 };
};

class DriverModel
{
public:
	static constexpr int kTapRate     = 32000;
	static constexpr int kSubsteps    = 2;
	static constexpr int kModes       = 4;
	static constexpr int kTraceLength = 8192;///< 0.256 s at 32 kHz, power of two

	DriverModel();

	/// New constants; the state is kept (a control change is not a reset).
	void Configure( const DriverSettings& settings );
	void Reset();

	/// Integrate through `n` tap samples, in volts.
	void Run( const float* volts, int n );

	//-- The trace --------------------------------------------------------------
	/// Samples integrated since Reset (the trace's newest index + 1).
	uint64_t Count() const
	{
		return count;
	}
	/// The state `back` samples before the newest, linearly interpolated;
	/// clamped to what the ring holds. back = 0 is the newest.
	Sample At( double back ) const;
	const Sample& Newest() const
	{
		return trace[ ( count + kTraceLength - 1 ) % kTraceLength ];
	}

	//-- What the model is ------------------------------------------------------
	double Bl( double x ) const;
	double Fsus( double x ) const;
	double Fbox( double x ) const;
	const Preset& Driver() const
	{
		return *preset;
	}
	double Xmech() const
	{
		return preset->Xmech;
	}
	double Xlin() const
	{
		return 0.5 * ( hvc - preset->Hg );
	}
	/// Half-width of the Bl plateau about the gap centre: Xlin - Hg/4.
	double Plateau() const
	{
		return Xlin() - 0.25 * preset->Hg;
	}
	double Offset() const
	{
		return x0;
	}
	double Alpha() const
	{
		return alpha;
	}
	double BoxVolume() const
	{
		return vb;
	}
	double Kms() const
	{
		return 1.0 / preset->Cms;
	}
	double Kbox() const
	{
		return alpha / preset->Cms;
	}
	double Mms() const
	{
		return mms;
	}
	int ModeCount() const
	{
		return modeCount;
	}
	double ModeHz( int n ) const
	{
		return modeHz[ n ];
	}
	double ModeGain( int n ) const
	{
		return modeGamma[ n ];
	}
	/// The voltage at which a full-scale DC input holds the cone at Xlin in a
	/// Qtc 0.707 box: Drive's 0 dB. Independent of the Box and Xmax controls.
	static double ReferenceVolts( const Preset& p );

	//-- Statistics since Reset ---------------------------------------------------
	double MaxAbsX() const
	{
		return maxAbsX;
	}
	uint64_t StopHits() const
	{
		return stopHits;
	}
	/// Absolute sample index (Count() at the time) of the latest stop hit.
	uint64_t LastHit() const
	{
		return lastHit;
	}

	Perturb perturb;

private:
	struct State
	{
		double x, v, i;
	};
	State Derivative( const State& s, double volts ) const;
	void Step( double v0, double v1, double v2, double v3 );
	void Push();

	const Preset* preset = nullptr;
	DriverSettings settings;
	double hvc = 0.0, x0 = 0.0, alpha = 0.0, vb = 0.0, mms = 0.0, blNorm = 1.0;

	// Modes: q_{k+1} = Phi q_k + G a_k (ZOH on the mean acceleration)
	int modeCount = 0;
	double modeHz[ kModes ]    = {};
	double modeGamma[ kModes ] = {};
	double phi[ kModes ][ 4 ]  = {};
	double gam[ kModes ][ 2 ]  = {};

	State state       = { 0.0, 0.0, 0.0 };
	double q[ kModes ][ 2 ] = {};
	double history[ 4 ]     = {};///< the last four input samples, oldest first

	std::vector< Sample > trace;
	uint64_t count = 0;

	double maxAbsX    = 0.0;
	uint64_t stopHits = 0;
	uint64_t lastHit  = 0;
};

} // namespace woofer
