/*
  ==============================================================================

    LoudnessMeterBar.cpp

  ==============================================================================
*/

#include "LoudnessMeterBar.h"

LoudnessMeterBar::LoudnessMeterBar()
{
}

void LoudnessMeterBar::setLevels (float momentaryLufs, float shortTermLufs, float integratedLufs)
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double dtSeconds = hasLastUpdate ? juce::jmax (0.0, (nowMs - lastUpdateMs) / 1000.0) : 0.0;
    lastUpdateMs = nowMs;
    hasLastUpdate = true;

    momentary  = smooth (momentary,  momentaryLufs,  dtSeconds);
    shortTerm  = smooth (shortTerm,  shortTermLufs,  dtSeconds);
    integrated = smooth (integrated, integratedLufs, dtSeconds);

    repaint();
}

float LoudnessMeterBar::smooth (float displayed, float target, double dtSeconds) noexcept
{
    // Can't meaningfully glide across a finite <-> infinite boundary (no
    // reading yet, or a reset, versus an actual value) - there's no
    // sensible intermediate value, so snap instead.
    if (std::isinf (displayed) || std::isinf (target))
        return target;

    const float alpha = (float) (1.0 - std::exp (-dtSeconds / (double) smoothingTimeConstantSeconds));
    return displayed + (target - displayed) * alpha;
}

float LoudnessMeterBar::normalise (float lufs) noexcept
{
    if (std::isinf (lufs))
        return 0.0f;

    return juce::jlimit (0.0f, 1.0f, (lufs - floorLufs) / (ceilingLufs - floorLufs));
}

void LoudnessMeterBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (findColour (trackColourId));
    g.fillRoundedRectangle (bounds, 4.0f);

    // The fill itself: height tracks momentary loudness, colour is a plain
    // low -> mid -> high ramp across the bar's own range (a classic VU
    // convention), independent of the reference lines below. These three
    // colours are deliberately still theme-controlled (not hardcoded green/
    // yellow/red) so a future palette can restyle the meter without code
    // changes, even though the low/mid/high *ordering* stays fixed.
    const float fillNorm = normalise (momentary);
    if (fillNorm > 0.0f)
    {
        auto fillBounds = bounds.withTop (bounds.getBottom() - bounds.getHeight() * fillNorm);

        juce::ColourGradient gradient (findColour (fillLowColourId), bounds.getBottomLeft(),
                                        findColour (fillHighColourId), bounds.getTopLeft(), false);
        gradient.addColour (0.7, findColour (fillMidColourId));
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (fillBounds, 4.0f);
    }

    // Reference gridlines for well-known loudness targets, with small
    // labels drawn directly on the bar.
    auto drawReferenceLine = [&] (float lufs, const juce::String& label)
    {
        const float y = bounds.getBottom() - bounds.getHeight() * normalise (lufs);
        const auto lineColour = findColour (referenceLineColourId);
        g.setColour (lineColour.withAlpha (0.45f));
        g.drawLine (bounds.getX(), y, bounds.getRight(), y, 1.0f);
        g.setColour (lineColour.withAlpha (0.8f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (label, juce::Rectangle<float> (bounds.getX() + 3.0f, y - 12.0f, bounds.getWidth() - 6.0f, 11.0f),
                    juce::Justification::left);
    };

    drawReferenceLine (streamingLufs, "-14");
    drawReferenceLine (ebuR128Lufs,   "-23");

    // Short-term and integrated readings as thin marker lines on top of the
    // momentary fill, so you can see "now" relative to "the trend" and "the
    // whole programme so far" at a glance.
    auto drawMarker = [&] (float lufs, juce::Colour colour)
    {
        if (std::isinf (lufs))
            return;

        const float y = bounds.getBottom() - bounds.getHeight() * normalise (lufs);
        g.setColour (colour);
        g.drawLine (bounds.getX(), y, bounds.getRight(), y, 2.0f);
    };

    drawMarker (shortTerm, findColour (shortTermMarkerColourId));
    drawMarker (integrated, findColour (integratedMarkerColourId));

    g.setColour (findColour (borderColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
}
