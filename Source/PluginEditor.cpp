/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
namespace
{
    // Embedded rather than referenced by name: a font requested by name
    // (e.g. juce::FontOptions ("Ubuntu", ...)) silently falls back to a
    // default typeface on any machine that doesn't have Ubuntu installed
    // system-wide - including a listener's machine after you export this
    // plugin. Embedding the .ttf data in the binary (via Projucer's
    // BINARYDATA resource mechanism) guarantees the exact glyphs are
    // always available, independent of the host system's installed fonts.
    // Parsing font data isn't free, so each typeface is created once and
    // reused (function-local static) rather than re-parsed per label.
    juce::Typeface::Ptr getUbuntuRegularTypeface()
    {
        static juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor (
            BinaryData::UbuntuRegular_ttf, (size_t) BinaryData::UbuntuRegular_ttfSize);
        return typeface;
    }

    juce::Typeface::Ptr getUbuntuBoldTypeface()
    {
        static juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor (
            BinaryData::UbuntuBold_ttf, (size_t) BinaryData::UbuntuBold_ttfSize);
        return typeface;
    }

    // Colours are read from the parent's LookAndFeel (LufsMonitorLookAndFeel)
    // and baked onto the label via setColour() at setup time - so this must
    // run after setLookAndFeel() has been called on the editor, or it'll
    // pick up whatever LookAndFeel was active before ours (JUCE's global
    // default), not the theme.
    void setupCaptionLabel (juce::Label& label, juce::Component& parent, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (getUbuntuRegularTypeface()).withHeight (14.0f));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, parent.findColour (Lufs_monitorAudioProcessorEditor::captionTextColourId));
        parent.addAndMakeVisible (label);
    }

    void setupValueLabel (juce::Label& label, juce::Component& parent)
    {
        label.setText ("-inf", juce::dontSendNotification);
        label.setFont (juce::FontOptions (getUbuntuBoldTypeface()).withHeight (20.0f));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, parent.findColour (Lufs_monitorAudioProcessorEditor::valueTextColourId));
        parent.addAndMakeVisible (label);
    }
}

Lufs_monitorAudioProcessorEditor::Lufs_monitorAudioProcessorEditor (Lufs_monitorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Must happen before any child component is created below: label setup
    // reads colours from this LookAndFeel via findColour(), and the meter
    // bars resolve theirs the same way the first time they paint.
    setLookAndFeel (&lookAndFeel);

    setupCaptionLabel (shortTermLabel,  *this, "LUFS");
    setupCaptionLabel (integratedLabel, *this, "Integrated");
    setupCaptionLabel (momentaryLabel,  *this, "Momentary");

    setupValueLabel (momentaryValueLabel,  *this);
    setupValueLabel (shortTermValueLabel,  *this);
    setupValueLabel (integratedValueLabel, *this);

    resetButton.onClick = [this] { audioProcessor.lufsMeter.requestReset(); };
    addAndMakeVisible (resetButton);

    addAndMakeVisible (meterBar_left);
    addAndMakeVisible (meterBar_right);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (180, 300);

    openGLContext.attachTo (*this);

    // Purely a display refresh rate - the meter itself keeps measuring on
    // the audio thread (and integrating on its own timer inside the
    // processor) whether or not this Timer, or the editor, exists at all.
    // Note: the underlying LUFS readings still only change every 100 ms
    // (that's the BS.1770 measurement window, not a display limit), so at
    // 60 Hz most frames will redraw the same value - see the follow-up
    // about adding interpolation if you want the bar to visibly glide
    // between updates rather than step.
    startTimerHz (60);
}

Lufs_monitorAudioProcessorEditor::~Lufs_monitorAudioProcessorEditor()
{
    // Must happen before this component's children are destroyed: detach()
    // blocks until the render thread has stopped touching this component
    // tree, so doing it first guarantees there's no window where the GL
    // thread could paint into components that are mid-teardown.
    openGLContext.detach();
    setLookAndFeel (nullptr);
    stopTimer();
}

//==============================================================================
void Lufs_monitorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void Lufs_monitorAudioProcessorEditor::resized()
{
    auto padding = 10;
    auto meter_width = 25;
    
    auto bounds = getLocalBounds().reduced (padding);

    auto bottomRow = bounds.removeFromBottom (padding * 2);
    resetButton.setBounds (bottomRow.withSizeKeepingCentre (100, 25));
    bounds.removeFromBottom (padding * 2);

    // Left meter
    meterBar_left.setBounds (bounds.removeFromLeft (meter_width));
    bounds.removeFromLeft (padding);
    
    // Right meter
    meterBar_right.setBounds (bounds.removeFromRight (meter_width));
    bounds.removeFromRight(padding);

    auto makeCaptionAndValue = [&bounds] (juce::Label& caption, juce::Label& value, int padding)
    {
        auto captionRow = bounds.removeFromTop (padding * 5);
        caption.setBounds (captionRow);
        // Value
        auto valueRow = bounds.removeFromTop (padding * 2);
        value.setBounds (valueRow);
//        caption.setBounds (row.removeFromLeft (row.getWidth() / 2));
//        value.setBounds (row);
    };

    makeCaptionAndValue (shortTermLabel,  shortTermValueLabel, padding);
    makeCaptionAndValue (integratedLabel, integratedValueLabel, padding);
    makeCaptionAndValue (momentaryLabel,  momentaryValueLabel, padding);
}

//==============================================================================
juce::String Lufs_monitorAudioProcessorEditor::formatLoudness (float lufs)
{
    if (std::isinf (lufs))
        return "-inf";

    return juce::String (lufs, 1);
}

void Lufs_monitorAudioProcessorEditor::timerCallback()
{
    const float momentaryLufs  = audioProcessor.lufsMeter.getMomentaryLoudness();
    const float shortTermLufs  = audioProcessor.lufsMeter.getShortTermLoudness();
    const float integratedLufs = audioProcessor.lufsMeter.getIntegratedLoudness();

    momentaryValueLabel.setText  (formatLoudness (momentaryLufs),  juce::dontSendNotification);
    shortTermValueLabel.setText  (formatLoudness (shortTermLufs),  juce::dontSendNotification);
    integratedValueLabel.setText (formatLoudness (integratedLufs), juce::dontSendNotification);

    meterBar_left.setLevels (momentaryLufs, shortTermLufs, integratedLufs);
    meterBar_right.setLevels (momentaryLufs, shortTermLufs, integratedLufs);
}
