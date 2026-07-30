/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
Lufs_monitorAudioProcessor::Lufs_monitorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    // Drains completed gating blocks into the integrated-loudness history
    // and recomputes the reading. Runs on the message thread regardless of
    // whether the editor is open, so integrated loudness keeps tracking the
    // whole programme even while the GUI is closed - see LufsMeter::pump()
    // for why that matters. 10 Hz matches the 100 ms subblock rate, so the
    // fixed-capacity FIFO never has a chance to back up.
    startTimerHz (10);
}

Lufs_monitorAudioProcessor::~Lufs_monitorAudioProcessor()
{
    stopTimer();
}

void Lufs_monitorAudioProcessor::timerCallback()
{
    lufsMeter.pump();
}

//==============================================================================
const juce::String Lufs_monitorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool Lufs_monitorAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool Lufs_monitorAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool Lufs_monitorAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double Lufs_monitorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int Lufs_monitorAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int Lufs_monitorAudioProcessor::getCurrentProgram()
{
    return 0;
}

void Lufs_monitorAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String Lufs_monitorAudioProcessor::getProgramName (int index)
{
    return {};
}

void Lufs_monitorAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void Lufs_monitorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    lufsMeter.prepare (sampleRate, getTotalNumInputChannels());
}

void Lufs_monitorAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool Lufs_monitorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void Lufs_monitorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // This plugin is a monitor, not an effect: it measures the signal and
    // passes it through unchanged, so the only thing processBlock() does
    // is feed the buffer to the loudness meter.
    lufsMeter.processBlock (buffer);
}

//==============================================================================
bool Lufs_monitorAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* Lufs_monitorAudioProcessor::createEditor()
{
    return new Lufs_monitorAudioProcessorEditor (*this);
}

//==============================================================================
void Lufs_monitorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void Lufs_monitorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Lufs_monitorAudioProcessor();
}
