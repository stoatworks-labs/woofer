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
    steps once a frame), it is uncalibrated (levels are relative), and it is
    frame-rate dependent.

    **The bins' law** is Resolume's as measured in Arena 7.29.0 (earshot's
    AGENTS.md, "Resolume's 64 bins, measured"): magnitude, linear, 64 bins
    of a 64th of the host's Nyquist each, 375 Hz at Arena's 48 kHz engine
    rate. That is FFT Law's default, Linear Mag; the Log and Power laws stay
    for hosts nobody has measured. The rate comes from FFGL SetSampleRate
    (SetHostRate). earshot shares each bin among the bands it overlaps
    (earshot 0.1.1), so the 20-200 Hz band takes 180/375 of bin 0's energy
    (its level 0.693 of the bin's) and 200-800 Hz the rest of bin 0 above
    200 Hz: 64 linear bins cannot say where inside 0-375 Hz the energy lies.
    (Snapshot 8124191 read the bins as linear to 22.05 kHz and gave each bin
    whole to the band holding its centre, so the 20-200 Hz band got all of
    0-375 Hz.)

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

	/// The host's audio engine rate (FFGL SetSampleRate), the axis of its FFT
	/// bins. Recorded when it changes, logged once, and handed to earshot by
	/// the next Pull (with FFT Law, in one configure). 0, or a rate no engine
	/// runs at, keeps what earshot has (48 kHz until a host says otherwise).
	void SetHostRate( unsigned int rate );

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
	/// The rate earshot reads the host's bins at (as applied).
	double HostRateForTest() const
	{
		return engine->config().hostBins.sampleRate;
	}
	bool fallbackFromTap = false;///< negative: the fallback reads the (empty) tap
	bool menuByIndex     = false;///< negative: element values are positions
	bool ignoreHostRate  = false;///< negative: the host's rate never reaches earshot

private:
	std::unique_ptr< earshot::Engine > engine;
	std::vector< DeviceSlot > slots;
	std::map< float, std::string > seen;///< every device key met, and its name
	float applied   = 0.0f;
	bool feeding    = false;
	int appliedLaw  = -1;
	unsigned int hostRate    = 0;///< SetHostRate's last valid rate, 0 none
	unsigned int appliedRate = 0;///< the rate the last configure carried
	bool lastFromDevice = false;

	double phase[ 4 ]     = {};
	double prevLevel[ 4 ] = {};
	double carry          = 0.0;
	std::vector< float > left, right;
};

} // namespace woofer
