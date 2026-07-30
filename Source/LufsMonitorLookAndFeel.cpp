/*
  ==============================================================================

    LufsMonitorLookAndFeel.cpp

  ==============================================================================
*/

#include "LufsMonitorLookAndFeel.h"
#include "LoudnessMeterBar.h"
#include "PluginEditor.h"

const juce::Colour LufsMonitorLookAndFeel::Palette::background     { 0xff1a1a1d };
const juce::Colour LufsMonitorLookAndFeel::Palette::panel          { 0xff232327 };
const juce::Colour LufsMonitorLookAndFeel::Palette::textPrimary    { 0xfff2f2f0 };
const juce::Colour LufsMonitorLookAndFeel::Palette::textSecondary  { 0xff9a9a9e };
const juce::Colour LufsMonitorLookAndFeel::Palette::accent         { 0xffffa726 };

// Deliberately not derived from the accent colour: green/amber/red on the
// meter communicate "quiet / getting loud / hot" independently of whatever
// the rest of the theme's accent happens to be - that convention should
// survive a future palette change even if the accent colour doesn't.
const juce::Colour LufsMonitorLookAndFeel::Palette::meterLow            { 0xff43a047 };
const juce::Colour LufsMonitorLookAndFeel::Palette::meterMid            { 0xffffb300 };
const juce::Colour LufsMonitorLookAndFeel::Palette::meterHigh           { 0xffe53935 };
const juce::Colour LufsMonitorLookAndFeel::Palette::meterReferenceLine  { 0xff4fc3f7 };

LufsMonitorLookAndFeel::LufsMonitorLookAndFeel()
{
    // Standard JUCE components.
    setColour (juce::ResizableWindow::backgroundColourId, Palette::background);

    setColour (juce::Label::textColourId, Palette::textPrimary);

    setColour (juce::TextButton::buttonColourId,   Palette::panel);
    setColour (juce::TextButton::buttonOnColourId, Palette::accent);
    setColour (juce::TextButton::textColourOffId,  Palette::accent);
    setColour (juce::TextButton::textColourOnId,   Palette::background);

    // This project's own components - see PluginEditor.h and
    // LoudnessMeterBar.h for what each ID controls.
    setColour (Lufs_monitorAudioProcessorEditor::captionTextColourId, Palette::textSecondary);
    setColour (Lufs_monitorAudioProcessorEditor::valueTextColourId,   Palette::textPrimary);

    setColour (LoudnessMeterBar::trackColourId,             Palette::panel);
    setColour (LoudnessMeterBar::fillLowColourId,           Palette::meterLow);
    setColour (LoudnessMeterBar::fillMidColourId,           Palette::meterMid);
    setColour (LoudnessMeterBar::fillHighColourId,          Palette::meterHigh);
    setColour (LoudnessMeterBar::referenceLineColourId,     Palette::meterReferenceLine);
    setColour (LoudnessMeterBar::shortTermMarkerColourId,   Palette::textPrimary);
    setColour (LoudnessMeterBar::integratedMarkerColourId,  Palette::accent);
    setColour (LoudnessMeterBar::borderColourId,            Palette::textSecondary.withAlpha (0.6f));
}
