#pragma once

#include "Controls.h"

#include <earshot/earshot.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace earshot::testing
{
class CaptureBackend;
}

/**
    Where the drive voltage comes from: earshot.

    **A device** (earshot's own capture, opened by its worker thread, never
    on the render thread): the waveform tap at 32 kHz, contiguous, every
    sample once. That is the voltage, times Drive -- the real path.

    **Resolume's FFT** (no device chosen, or a chosen one Opening, Missing or
    Failed): earshot's fallback gives band envelopes only, once a frame, with
    no phase and no waveform, and the tap is empty. So a voltage is
    SYNTHESISED: one sine per band at the band's geometric centre (63, 400,
    1600, 6400 Hz), its amplitude ramped across the frame from the band's last
    level to this one, its phase continuous. It pumps the cone on the low band
    and barely moves it on the high one, and every nonlinearity still acts on
    it; but it is not the music's waveform (a kick is a 63 Hz sine whose level
    steps once a frame), it is uncalibrated (nobody has measured Resolume's
    bins: FFT Law is the assumption, exposed), and it is frame-rate dependent.

    **The device menu** follows spasis, with earshot's rule that a device is
    remembered by name: element 0 "None (host FFT)" = 0, element 1 "System
    default" = deviceKey( kDefaultDevice ), then each device with
    deviceKey( name ), then "(none)" spares (value 0), kInputSlots in all.
    `Select` is called only when the value changes, so the initial state
    never calls selectDevice( "" ) -- which would end a feed().
*/
namespace woofer
{

using BackendFactory = std::unique_ptr< earshot::testing::CaptureBackend > ( * )();

struct DeviceSlot
{
	std::string label;///< what the menu shows (30 characters at most)
	std::string name; ///< what selectDevice is given ("" for the host FFT)
	float value;      ///< the element's value: the device key
};

class AudioInput
{
public:
	/// For the harness: every AudioInput constructed afterwards uses this
	/// backend (a scripted device) instead of miniaudio, and lists ITS
	/// devices. Set before any plugin exists; never set by the plugin.
	static void SetBackendFactoryForTest( BackendFactory factory );

	AudioInput();
	~AudioInput();

	/// Enumerate again (constructor and Rescan). Never per frame.
	void Refresh();
	const std::vector< DeviceSlot >& Slots() const
	{
		return slots;
	}

	/// The Audio Input value the host stored; acts only when it changes.
	void Select( float value );

	/// This frame's voltage samples at DriverModel::kTapRate into `out`.
	/// `hostSeconds` is the host clock (seconds), `dt` this frame's step of
	/// it (the fallback's sample count).
	int Pull( double hostSeconds, double dt, const float* bins, int binCount, Channel channel, FftLaw law, double volts,
	          std::vector< float >& out );

	const earshot::Frame& Frame() const;
	earshot::CaptureState State() const;
	bool FromDevice() const
	{
		return lastFromDevice;
	}

	//-- Harness ------------------------------------------------------------------
	/// Audio as if from a device, without one: earshot's feed().
	void Feed( const float* interleaved, int frames, int rate, int channels );
	earshot::Engine& Engine()
	{
		return *engine;
	}
	bool fallbackFromTap = false;///< negative: the fallback reads the (empty) tap
	bool menuByIndex     = false;///< negative: element values are positions

private:
	std::unique_ptr< earshot::Engine > engine;
	std::vector< DeviceSlot > slots;
	std::map< float, std::string > seen;///< every device key met, and its name
	float applied   = 0.0f;
	bool feeding    = false;
	int appliedLaw  = -1;
	bool lastFromDevice = false;

	double phase[ 4 ]     = {};
	double prevLevel[ 4 ] = {};
	double carry          = 0.0;
	std::vector< float > left, right;
};

} // namespace woofer
