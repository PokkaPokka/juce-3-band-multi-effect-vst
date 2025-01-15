/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
*/
class _3BandMultiEffectorAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    _3BandMultiEffectorAudioProcessor();
    ~_3BandMultiEffectorAudioProcessor() override;

    //==============================================================================
    // Prepare resources before playback starts
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    // Clean up resoruces when playback stops
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif
    
    // The host sends buffers at a regular rate, and this method renders the next block
    // Where the audio processing happens
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    // State management: saves and restores plugin states
    // Ensuring settings persist between sessions
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    // Parameter management: managing parameters in a structured and automatable way
    // Allowing integrating sliders, knobs, and other UI elements in the plugin editor
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts{*this, nullptr, "Parameters", createParameterLayout()};

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (_3BandMultiEffectorAudioProcessor)
};
