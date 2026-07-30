/*
  ==============================================================================

    LufsMonitorLookAndFeel: the plugin's single source of truth for colour.

    Every colour used anywhere in the editor - standard JUCE components
    (Label, TextButton, the plugin window background) and this project's own
    custom-painted components (LoudnessMeterBar) - is set here, once, via
    JUCE's colour ID system. Nothing else in the codebase should contain a
    hardcoded juce::Colours::* constant for UI chrome; components look their
    colours up with findColour() instead, so retheming the whole plugin is a
    matter of editing this one file.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class LufsMonitorLookAndFeel  : public juce::LookAndFeel_V4
{
public:
    LufsMonitorLookAndFeel();

    // "Dark charcoal + amber accent" palette. Kept as named constants
    // (rather than inlined hex literals in the constructor) so the handful
    // of values that actually define the theme's identity are easy to find
    // and swap for a different palette later.
    struct Palette
    {
        static const juce::Colour background;
        static const juce::Colour panel;
        static const juce::Colour textPrimary;
        static const juce::Colour textSecondary;
        static const juce::Colour accent;

        static const juce::Colour meterLow;
        static const juce::Colour meterMid;
        static const juce::Colour meterHigh;
        static const juce::Colour meterReferenceLine;
    };
};
