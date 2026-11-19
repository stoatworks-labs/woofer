#pragma once

#include "earshot/earshot.h"
#include "earshot/testing.h"

/**
	The fallback: band envelopes and onsets from Resolume's FFT buffer, when no
	device is chosen (or a chosen one is down and Config allows it).

	## What is measured, and what is assumed

	The host fills an `FF_USAGE_FFT` buffer once per rendered frame: 64 floats.
	Resolume's law was measured in Arena 7.29.0 (earshot.h, HostBins; AGENTS.md):
	magnitude, linear, bin j spanning [j, j+1)/64 of the engine rate's Nyquist,
	each bin the sum of the finer FFT's magnitudes inside its span with almost
	no leakage, refreshed about every 43 ms (2048 samples at 48 kHz, by the
	intervals measured) and held in between. The law stays HostBins, configuration a plugin may expose.

	**A bin is shared among the bands its span overlaps**, in proportion to
	the overlap. Since the bin says nothing about where inside its span the
	energy lies, the share is the expected energy for any position of a tone
	and the exact energy of a flat spectrum. A band inside one bin (a kick
	band of 40-160 Hz inside bin 0's 0-375 Hz) gets its share; a band is empty
	only where no bin reaches. (Until 0.1.1 a bin went whole to the band its
	centre fell in, and such a band got nothing: `hostCentreOnly`.) Nothing here
	is calibrated -- levels are relative, `Frame::calibrated` is false -- and
	everything the bins cannot carry (pitch, partials, thirds, tempo, stereo,
	the waveform) stays EMPTY rather than guessed: spasis's rule, a plausible
	guess is worse than an empty display.

	## Onsets on video frames

	Linear magnitude flux per band against an adaptive floor, as pattern does
	on the same bins. Primed on the first frame (previous bins = current,
	floor = an eighth of the band's level). The frame interval comes from the
	host clock as a difference in double; a repeated or backward time steps
	nothing (dt = 0 must not snap the floor: the fleet's bug), and a gap over a
	second re-primes, because flux across a seek is not an onset.

	**Thread**: render thread, inside Engine::update. No allocation.
*/
namespace earshot
{

class HostAnalyser
{
public:
	static constexpr int kMaxBins = 256;

	void reset() noexcept;
	void configure( const Config& config ) noexcept;
	void frame( double hostSeconds, const float* bins, int count, const Config& config,
				const testing::Mutations& mut ) noexcept;
	void publish( Band* out, int& count ) noexcept;

	/// The span [lo, hi) of bin j of `count` under the law, in Hz.
	static void binSpan( int j, int count, const HostBins& law, double& lo, double& hi ) noexcept;
	/// Centre frequency of bin j of `count` under the law.
	static double binHz( int j, int count, const HostBins& law ) noexcept;
	/// The fraction of a bin's span [binLo, binHi) that lies in [bandLo, bandHi).
	static double share( double binLo, double binHi, double bandLo, double bandHi ) noexcept;

	double clock() const noexcept { return now_ - origin_; }
	/// The loudest bin's frequency (for the negative control only).
	double loudestHz() const noexcept { return loudestHz_; }

private:
	struct State
	{
		float  loHz = 0, hiHz = 0;
		double floor = 0, prevFlux = 0, prevEnergy = 0, level = 0, envelope = 0;
		double lastFire = -1e9, strength = 0, lastTime = -1;
		bool   onset = false;
		bool   above = false;   ///< flux was over the threshold on the previous frame
		uint32_t count = 0;
	};

	State  bands_[ kMaxBands ];
	int    bandCount_ = 0;
	double prev_[ kMaxBins ] = {};
	bool   primed_ = false;
	double last_ = 0.0, now_ = 0.0, origin_ = 0.0;
	double loudestHz_ = 0.0;
};

} // namespace earshot
