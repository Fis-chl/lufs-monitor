/*
  ==============================================================================

    LufsMeter: an ITU-R BS.1770-4 compliant loudness meter.

    Computes Momentary (400 ms), Short-term (3 s) and Integrated (gated,
    programme-length) loudness in LUFS. See LufsMeter.cpp for a full
    explanation of the algorithm and the threading model.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

class LufsMeter
{
public:
    LufsMeter();

    /** Call from AudioProcessor::prepareToPlay(). Not safe to call
        concurrently with processBlock() (matches the JUCE contract that
        hosts never do so). */
    void prepare (double sampleRate, int numChannels);

    /** Call once per audio block, from the audio thread. Read-only with
        respect to the incoming buffer - this class never modifies audio. */
    void processBlock (const juce::AudioBuffer<float>& buffer);

    /** Thread-safe: request that all readings be cleared. Safe to call from
        the message thread (e.g. a "Reset" button) at any time, including
        while audio is running. */
    void requestReset();

    /** Call periodically from the message thread (e.g. a Timer at ~10 Hz)
        to drain newly completed gating blocks and recompute integrated
        loudness. Must be called regularly for the whole lifetime of the
        plugin instance, not just while the editor is open - otherwise the
        internal FIFO can overflow and gating blocks will be silently lost. */
    void pump();

    float getMomentaryLoudness()  const noexcept { return momentaryLoudness.load (std::memory_order_relaxed); }
    float getShortTermLoudness()  const noexcept { return shortTermLoudness.load (std::memory_order_relaxed); }
    float getIntegratedLoudness() const noexcept { return integratedLoudness.load (std::memory_order_relaxed); }

private:
    //==============================================================================
    // A single second-order IIR section (Direct Form II Transposed), used to
    // build the two-stage K-weighting filter. DF2T is preferred here because
    // its internal state (z1, z2) has lower round-off error sensitivity than
    // Direct Form I for the coefficient ranges K-weighting produces.
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        inline float processSample (float x) noexcept
        {
            const double in  = (double) x;
            const double out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            return (float) out;
        }

        void reset() noexcept { z1 = 0.0; z2 = 0.0; }
    };

    void computeKWeightingCoefficients (double sampleRate);
    void finishSubblock();
    void resetAudioState();
    void resetUiState();
    double computeIntegratedLoudness() const;

    //==============================================================================
    // --- Audio-thread-owned state (only ever touched from processBlock) ---
    double sampleRate = 44100.0;
    int numChannels = 2;

    juce::Array<Biquad> stage1;          // per-channel high-shelf stage
    juce::Array<Biquad> stage2;          // per-channel RLB high-pass stage
    juce::Array<double> channelWeights;  // per-channel BS.1770 channel weight
    juce::Array<double> subblockSumSquares;

    int samplesPerSubblock = 4410;       // ~100 ms, recalculated in prepare()
    int subblockSampleCounter = 0;

    static constexpr int shortTermBlocks = 30; // 3 s / 100 ms
    std::array<double, shortTermBlocks> subblockHistory {};
    int historyWriteIndex = 0;
    int historyFilledCount = 0;

    std::atomic<bool> audioResetPending { false };

    // --- Lock-free single-producer (audio thread) / single-consumer
    //     (pump(), message thread) channel for completed 400 ms gating
    //     blocks headed for the integrated-loudness calculation. ---
    static constexpr int fifoCapacity = 8192; // ~13.6 minutes at 100ms/slot
    juce::AbstractFifo gatingBlockFifo { fifoCapacity };
    std::array<double, fifoCapacity> gatingBlockBuffer {};

    // --- Message-thread-owned state (only ever touched from pump()) ---
    std::atomic<bool> uiResetPending { false };
    std::vector<double> integratedHistory;

    // --- Cross-thread readouts (audio thread writes, any thread reads) ---
    std::atomic<float> momentaryLoudness  { -std::numeric_limits<float>::infinity() };
    std::atomic<float> shortTermLoudness  { -std::numeric_limits<float>::infinity() };
    std::atomic<float> integratedLoudness { -std::numeric_limits<float>::infinity() };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LufsMeter)
};
