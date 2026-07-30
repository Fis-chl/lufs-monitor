/*
  ==============================================================================

    LoudnessMeterBar: a vertical bar-graph display for LUFS readings.

    The filled bar tracks momentary loudness (the most responsive of the
    three readings - it's what "how loud is it right now" means). Short-term
    and integrated loudness are drawn as thin marker lines on top of it, so
    you can see at a glance whether the current moment is louder or quieter
    than the recent trend / the whole programme.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class LoudnessMeterBar  : public juce::Component
{
public:
    // Custom colour IDs, set via a LookAndFeel (LookAndFeel_V4::setColour)
    // exactly like JUCE's own built-in components. Values are offset well
    // clear of JUCE's built-in IDs (which live below 0x2000000) so they
    // can't collide. Anything not explicitly set by the active LookAndFeel
    // falls back to whatever LookAndFeel_V4::getDefaultColour() gives an
    // unrecognised ID (usually black), so a theme should set all of these.
    enum ColourIds
    {
        trackColourId            = 0x2000000,
        fillLowColourId          = 0x2000001,
        fillMidColourId          = 0x2000002,
        fillHighColourId         = 0x2000003,
        referenceLineColourId    = 0x2000004,
        shortTermMarkerColourId  = 0x2000005,
        integratedMarkerColourId = 0x2000006,
        borderColourId           = 0x2000007,
    };

    LoudnessMeterBar();

    /** Feed in the current true readings. Call as often as you like (e.g.
        every frame from an editor Timer) - the values actually drawn ease
        towards these targets rather than jumping straight to them, so
        calling this faster than the readings themselves change (they only
        update every ~100 ms) is what makes the bar glide instead of step.
        Call from the message thread - this class does no thread-safety of
        its own. */
    void setLevels (float momentaryLufs, float shortTermLufs, float integratedLufs);

    void paint (juce::Graphics&) override;

private:
    // The dB range the bar covers. -60 LUFS is a practical "silence" floor
    // for display purposes (well below the -70 LUFS absolute gate, so the
    // gate itself is never visible as a dead zone at the bottom of the bar).
    static constexpr float floorLufs    = -60.0f;
    static constexpr float ceilingLufs  =   0.0f;

    // Reference lines for common loudness targets, purely to give the bar
    // some visual context. -23 LUFS is the EBU R128 broadcast target;
    // -14 LUFS is the de facto streaming-platform target (Spotify, YouTube,
    // etc). Neither is treated as "correct" for any particular delivery -
    // they're just widely recognisable landmarks.
    static constexpr float ebuR128Lufs      = -23.0f;
    static constexpr float streamingLufs    = -14.0f;

    static float normalise (float lufs) noexcept;

    // One-pole ("RC") smoothing: on each call to setLevels(), the displayed
    // value moves a fraction of the way to the new target, where that
    // fraction depends on how much time has actually elapsed since the
    // last call. This is time-based rather than a fixed per-frame step so
    // the glide looks the same regardless of the UI's actual frame rate
    // (which a host is free to throttle under load). 100 ms means the
    // displayed value covers ~63% of the remaining distance to a new
    // target every 100 ms - about one measurement update period - so it
    // visibly glides without feeling laggy behind the real reading.
    static constexpr float smoothingTimeConstantSeconds = 0.1f;

    static float smooth (float displayed, float target, double dtSeconds) noexcept;

    double lastUpdateMs = 0.0;
    bool hasLastUpdate = false;

    // These hold the smoothed, currently-displayed values (what paint()
    // draws), not the raw instantaneous readings passed into setLevels().
    float momentary  = -std::numeric_limits<float>::infinity();
    float shortTerm  = -std::numeric_limits<float>::infinity();
    float integrated = -std::numeric_limits<float>::infinity();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessMeterBar)
};
