/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LoudnessMeterBar.h"
#include "LufsMonitorLookAndFeel.h"

//==============================================================================
/**
*/
class Lufs_monitorAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    // See LoudnessMeterBar::ColourIds for the pattern this follows. Offset
    // clear of both JUCE's built-in IDs and LoudnessMeterBar's own, so all
    // three ID spaces can coexist in one LookAndFeel without collisions.
    enum ColourIds
    {
        captionTextColourId = 0x2000100,
        valueTextColourId   = 0x2000101,
    };

    Lufs_monitorAudioProcessorEditor (Lufs_monitorAudioProcessor&);
    ~Lufs_monitorAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    static juce::String formatLoudness (float lufs);

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    Lufs_monitorAudioProcessor& audioProcessor;

    // Every colour in the editor is set here - see LufsMonitorLookAndFeel.cpp.
    LufsMonitorLookAndFeel lookAndFeel;

    // Renders this editor via OpenGL instead of the default CPU (CoreGraphics
    // on macOS) path. Must be detached before the component tree it's
    // attached to starts tearing down - see the destructor.
    juce::OpenGLContext openGLContext;

    // Left and right meter bar
    LoudnessMeterBar meterBar_left;
    LoudnessMeterBar meterBar_right;

    juce::Label momentaryLabel, momentaryValueLabel;
    juce::Label shortTermLabel, shortTermValueLabel;
    juce::Label integratedLabel, integratedValueLabel;
    juce::TextButton resetButton { "Reset" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Lufs_monitorAudioProcessorEditor)
};
