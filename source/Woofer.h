#pragma once

#include "Audio.h"
#include "Controls.h"
#include "Driver.h"
#include "Profile.h"

#include <FFGLSDK.h>
// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <vector>

namespace woofer
{

/**
    The plugin: `SW Woofer`, the clip printed on a loudspeaker the music
    drives. Each frame: the host clock; earshot's audio, as volts at 32 kHz;
    the driver integrated through every sample; the profile table for each
    exposure instant; one fragment pass to the host.

    The host index is the ParamId (Controls.h).
*/
class WooferPlugin : public CFFGLPlugin
{
public:
	WooferPlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	DriverModel& ModelForTest()
	{
		return model;
	}
	Profile& ProfileForTest()
	{
		return profile;
	}
	AudioInput& AudioForTest()
	{
		return audio;
	}
	/// Audio as if from a device: earshot's feed(), interleaved floats.
	void FeedForTest( const float* interleaved, int frames, int rate, int channels )
	{
		audio.Feed( interleaved, frames, rate, channels );
	}
	/// One frame's audio and integration with no GL: the physics checks.
	/// `hostSeconds` stands in for the settled host clock.
	void AdvanceForTest( double hostSeconds, double dt );
	/// The volts the last frame integrated (DriverModel::kTapRate).
	const std::vector< float >& VoltsForTest() const
	{
		return volts;
	}
	/// The negative controls: the model's and the profile's.
	void SetPerturbForTest( const Perturb& p, const ProfileSettings& flags );
	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}
	void SetResetOnResizeForTest( bool on )
	{
		resetOnResize = on;
	}
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	/// The clock as settled: 1 seconds, 0.001 milliseconds, 0 not yet.
	double ClockScale() const
	{
		return clockScale;
	}
	double HostNow() const
	{
		return now;
	}
	/// The lamp's centre in pixels from the bottom left, at the last frame.
	double LampX() const
	{
		return lampPixelX;
	}
	double LampY() const
	{
		return lampPixelY;
	}
	/// Microseconds the last frame spent integrating the driver and building
	/// the profile table (the CPU's share).
	double IntegrateMicros() const
	{
		return integrateMicros;
	}
	double TableMicros() const
	{
		return tableMicros;
	}
	double AudioMicros() const
	{
		return audioMicros;
	}
	int RowsLastFrame() const
	{
		return rows;
	}
	int SamplesLastFrame() const
	{
		return static_cast< int >( volts.size() );
	}

private:
	void UpdateClock();
	void ApplySettings();
	void Advance( double dt );
	void Lamp( int width, int height, float colour[ 4 ], float pos[ 2 ] );
	GLint where( const char* name );

	float params[ PT_COUNT ] = {};

	AudioInput audio;
	DriverModel model;
	Profile profile;
	Perturb perturb;
	ProfileSettings profileFlags;
	int perturbGeneration = 0, appliedGeneration = -1;
	DriverSettings appliedDriver;
	ProfileSettings appliedProfile;
	bool configured = false;

	std::vector< float > volts;
	std::vector< float > table;///< Bins x 8 floats x kRows

	ffglex::FFGLShader shader;
	GLuint vao = 0, tableTexture = 0;
	std::vector< std::pair< const char*, GLint > > uniformCache;

	// The clock (ferrofluid's vote on seconds or milliseconds, fountain's patience).
	double hostTime     = -1.0;
	double clockScale   = 0.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	int clockSamples    = 0;
	bool settledJump    = false;
	double now          = 0.0;
	double lastNow      = -1.0;

	bool rescanHeld   = false;
	int hooks         = 0;
	bool resetOnResize = false;
	int lastWidth = 0, lastHeight = 0;
	int rows = 1;
	double lampPixelX = 0.0, lampPixelY = 0.0;
	double integrateMicros = 0.0, tableMicros = 0.0, audioMicros = 0.0;
};

} // namespace woofer
