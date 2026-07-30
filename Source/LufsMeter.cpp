/*
  ==============================================================================

    LufsMeter.cpp

    Implements ITU-R BS.1770-4 loudness measurement:

      1. K-weighting: a two-stage IIR filter that approximates how the ear
         perceives loudness across frequency (a shelf boost around 4 kHz for
         head diffraction, a high-pass roll-off below ~40 Hz).
      2. Mean-square power is accumulated in 100 ms "subblocks".
      3. Momentary loudness = last 400 ms (4 subblocks), Short-term = last
         3 s (30 subblocks), both ungated.
      4. Integrated loudness = programme-length average with two-stage
         gating (absolute gate at -70 LUFS, relative gate at -10 LU below
         the ungated mean), per BS.1770-4 Annex 2.

    See the class-level comments in LufsMeter.h for the threading model.

  ==============================================================================
*/

#include "LufsMeter.h"

namespace
{
    // z is a mean-square power value (linear, not dB). Converts to LUFS
    // using the -0.691 dB calibration offset defined by BS.1770-4.
    double zToLoudness (double z) noexcept
    {
        if (z <= 1.0e-15)
            return -std::numeric_limits<double>::infinity();

        return -0.691 + 10.0 * std::log10 (z);
    }
}

//==============================================================================
LufsMeter::LufsMeter()
{
}

void LufsMeter::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = juce::jmax (1, newNumChannels);

    stage1.clearQuick();
    stage2.clearQuick();
    channelWeights.clearQuick();
    subblockSumSquares.clearQuick();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        stage1.add (Biquad {});
        stage2.add (Biquad {});
        subblockSumSquares.add (0.0);

        // BS.1770 channel weights: L/R/C = 1.0, Ls/Rs = 1.41 (+1.5 dB), LFE
        // excluded. This scaffold only declares mono/stereo buses (see
        // isBusesLayoutSupported in PluginProcessor), where every channel is
        // front L/R, so a flat 1.0 is correct. If you later add multichannel
        // bus support, this is the place to map channel index -> weight
        // using the bus's AudioChannelSet, not just channel count.
        channelWeights.add (1.0);
    }

    computeKWeightingCoefficients (sampleRate);

    samplesPerSubblock = juce::jmax (1, (int) std::round (sampleRate * 0.1));
    subblockSampleCounter = 0;

    // Safe to mutate audio-owned state directly here: JUCE guarantees
    // prepareToPlay() is never called concurrently with processBlock().
    resetAudioState();

    // The integrated-loudness history lives on the message-thread side and
    // is only ever touched from pump(), so it can't be cleared directly
    // here without a data race - flag it instead.
    uiResetPending.store (true, std::memory_order_relaxed);
}

void LufsMeter::computeKWeightingCoefficients (double fs)
{
    // These constants (f0, G, Q for both stages) are the analog filter
    // design parameters published for the BS.1770 K-weighting curve. The
    // official standard only tabulates the resulting digital coefficients
    // at 48 kHz - a very common implementation bug is to hard-code that
    // 48 kHz table and use it unmodified at other sample rates (44.1 kHz,
    // 96 kHz, ...), which quietly shifts the filter's corner frequencies
    // and produces wrong readings. Recomputing from these parameters via a
    // pre-warped bilinear transform (K = tan(pi * f0 / fs)) keeps the
    // filter accurate at any sample rate, and reduces to the exact
    // published 48 kHz table when fs = 48000.

    // --- Stage 1: high-shelf, models head diffraction (+4 dB @ ~4 kHz) ---
    {
        const double f0 = 1681.9744509555319;
        const double G  = 3.999843853973347; // dB
        const double Q  = 0.7071752369554196;

        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        Biquad coeffs;
        coeffs.b0 = (Vh + Vb * K / Q + K * K) / a0;
        coeffs.b1 = 2.0 * (K * K - Vh) / a0;
        coeffs.b2 = (Vh - Vb * K / Q + K * K) / a0;
        coeffs.a1 = 2.0 * (K * K - 1.0) / a0;
        coeffs.a2 = (1.0 - K / Q + K * K) / a0;

        for (auto& f : stage1)
        {
            f.b0 = coeffs.b0; f.b1 = coeffs.b1; f.b2 = coeffs.b2;
            f.a1 = coeffs.a1; f.a2 = coeffs.a2;
        }
    }

    // --- Stage 2: RLB weighting, a high-pass roll-off below ~38 Hz ---
    {
        const double f0 = 38.13547087602444;
        const double Q  = 0.5003270373238773;

        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;

        Biquad coeffs;
        coeffs.b0 = 1.0;
        coeffs.b1 = -2.0;
        coeffs.b2 = 1.0;
        coeffs.a1 = 2.0 * (K * K - 1.0) / a0;
        coeffs.a2 = (1.0 - K / Q + K * K) / a0;

        for (auto& f : stage2)
        {
            f.b0 = coeffs.b0; f.b1 = coeffs.b1; f.b2 = coeffs.b2;
            f.a1 = coeffs.a1; f.a2 = coeffs.a2;
        }
    }
}

void LufsMeter::processBlock (const juce::AudioBuffer<float>& buffer)
{
    // One-shot consumption of a reset request. This runs on the audio
    // thread so it's safe to touch audio-owned state directly; a UI button
    // click just flips the flag from another thread via requestReset().
    if (audioResetPending.exchange (false, std::memory_order_relaxed))
        resetAudioState();

    const int numSamples = buffer.getNumSamples();
    const int channelsToProcess = juce::jmin (numChannels, buffer.getNumChannels());

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < channelsToProcess; ++ch)
        {
            const float x  = buffer.getSample (ch, i);
            const float y1 = stage1.getReference (ch).processSample (x);
            const float y2 = stage2.getReference (ch).processSample (y1);

            subblockSumSquares.getReference (ch) += (double) y2 * (double) y2;
        }

        if (++subblockSampleCounter >= samplesPerSubblock)
        {
            subblockSampleCounter = 0;
            finishSubblock();
        }
    }
}

void LufsMeter::finishSubblock()
{
    // Combine every channel's mean-square power for this ~100 ms subblock
    // into a single BS.1770-weighted power value "z". Because averaging is
    // linear, this per-subblock z can be reused directly as one sample of
    // both the momentary/short-term sliding windows AND the integrated-
    // loudness gating-block series below - no need to keep raw per-channel
    // history around.
    double z = 0.0;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const double meanSquare = subblockSumSquares.getReference (ch) / (double) samplesPerSubblock;
        z += channelWeights.getReference (ch) * meanSquare;
        subblockSumSquares.set (ch, 0.0);
    }

    subblockHistory[(size_t) historyWriteIndex] = z;
    historyWriteIndex = (historyWriteIndex + 1) % shortTermBlocks;
    if (historyFilledCount < shortTermBlocks)
        ++historyFilledCount;

    // Momentary loudness: mean of the last 4 subblocks (400 ms), updated
    // every 100 ms (75% overlap) - exactly as BS.1770-4 defines both the
    // momentary loudness window AND the gating block for integrated
    // loudness, so the same value feeds both.
    if (historyFilledCount >= 4)
    {
        double sum = 0.0;
        for (int i = 0; i < 4; ++i)
        {
            const int idx = (historyWriteIndex - 1 - i + shortTermBlocks) % shortTermBlocks;
            sum += subblockHistory[(size_t) idx];
        }
        const double momentaryZ = sum / 4.0;
        momentaryLoudness.store ((float) zToLoudness (momentaryZ), std::memory_order_relaxed);

        // Hand this gating block to the message thread. The FIFO is fixed
        // capacity and lock-free; if pump() isn't being called often enough
        // (see LufsMeter.h), getFreeSpace() will hit 0 and we drop blocks
        // rather than block the audio thread or allocate.
        if (gatingBlockFifo.getFreeSpace() > 0)
        {
            int start1, size1, start2, size2;
            gatingBlockFifo.prepareToWrite (1, start1, size1, start2, size2);
            if (size1 > 0)
                gatingBlockBuffer[(size_t) start1] = momentaryZ;
            gatingBlockFifo.finishedWrite (size1 + size2);
        }
    }

    // Short-term loudness: mean of the last 30 subblocks (3 s), ungated.
    if (historyFilledCount >= shortTermBlocks)
    {
        double sum = 0.0;
        for (double v : subblockHistory)
            sum += v;
        const double shortTermZ = sum / (double) shortTermBlocks;
        shortTermLoudness.store ((float) zToLoudness (shortTermZ), std::memory_order_relaxed);
    }
}

void LufsMeter::requestReset()
{
    // Both flags are set together but consumed independently (one exchange
    // per consumer) so a single request reliably resets both the audio-
    // owned and UI-owned halves of the meter, however audio and pump() are
    // interleaved in time.
    audioResetPending.store (true, std::memory_order_relaxed);
    uiResetPending.store (true, std::memory_order_relaxed);
}

void LufsMeter::resetAudioState()
{
    for (auto& f : stage1) f.reset();
    for (auto& f : stage2) f.reset();

    for (int ch = 0; ch < subblockSumSquares.size(); ++ch)
        subblockSumSquares.set (ch, 0.0);

    subblockSampleCounter = 0;
    historyWriteIndex = 0;
    historyFilledCount = 0;
    subblockHistory.fill (0.0);

    momentaryLoudness.store (-std::numeric_limits<float>::infinity(), std::memory_order_relaxed);
    shortTermLoudness.store (-std::numeric_limits<float>::infinity(), std::memory_order_relaxed);
}

void LufsMeter::resetUiState()
{
    integratedHistory.clear();
    integratedLoudness.store (-std::numeric_limits<float>::infinity(), std::memory_order_relaxed);

    // Discard (don't consume into history) anything still queued from
    // before the reset.
    int start1, size1, start2, size2;
    gatingBlockFifo.prepareToRead (gatingBlockFifo.getNumReady(), start1, size1, start2, size2);
    gatingBlockFifo.finishedRead (size1 + size2);
}

void LufsMeter::pump()
{
    if (uiResetPending.exchange (false, std::memory_order_relaxed))
        resetUiState();

    int start1, size1, start2, size2;
    gatingBlockFifo.prepareToRead (gatingBlockFifo.getNumReady(), start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        integratedHistory.push_back (gatingBlockBuffer[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i)
        integratedHistory.push_back (gatingBlockBuffer[(size_t) (start2 + i)]);

    gatingBlockFifo.finishedRead (size1 + size2);

    integratedLoudness.store ((float) computeIntegratedLoudness(), std::memory_order_relaxed);
}

double LufsMeter::computeIntegratedLoudness() const
{
    // BS.1770-4 Annex 2 two-stage gating:
    //   1) Absolute gate: discard blocks quieter than -70 LUFS (this is
    //      what keeps true digital silence, or long silent passages, from
    //      dragging the integrated reading down).
    //   2) Relative gate: compute the mean of the surviving blocks, form a
    //      second threshold 10 LU below that mean, and discard blocks
    //      quieter than *that* (this is what stops quiet passages, which
    //      are perceptually part of the programme, from being weighted
    //      equally with loud ones - it's what makes a mostly-loud track
    //      with a quiet intro read close to its loud-section level).
    //   3) Integrated loudness = mean power of blocks surviving both gates.
    constexpr double absoluteGateLufs = -70.0;

    std::vector<double> passedAbsolute;
    passedAbsolute.reserve (integratedHistory.size());

    for (double z : integratedHistory)
        if (zToLoudness (z) >= absoluteGateLufs)
            passedAbsolute.push_back (z);

    if (passedAbsolute.empty())
        return -std::numeric_limits<double>::infinity();

    double meanZAbs = 0.0;
    for (double z : passedAbsolute)
        meanZAbs += z;
    meanZAbs /= (double) passedAbsolute.size();

    const double relativeGateLufs = zToLoudness (meanZAbs) - 10.0;

    double sumZRel = 0.0;
    int countRel = 0;
    for (double z : passedAbsolute)
    {
        if (zToLoudness (z) >= relativeGateLufs)
        {
            sumZRel += z;
            ++countRel;
        }
    }

    if (countRel == 0)
        return -std::numeric_limits<double>::infinity();

    return zToLoudness (sumZRel / (double) countRel);
}
