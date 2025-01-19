/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

enum Slope
{
    Slope_12,
    Slope_24,
    Slope_36,
    Slope_48
};

struct ChainSettings
{
    float peakFreq {0}, peakGainInDeciibels{0}, peakQuality{1.f};
    float lowCutFreq{0}, highCutFreq{0};
    Slope lowCutSlope {Slope::Slope_12}, highCutSlope {Slope::Slope_12};
};

// A reference to the TreeState, which manages and connects parameter states in
// plugins to the actual processing logic
ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);

// Defines Filter as an alias for the JUCE Infinite Impulse Response (IIR) filter,
// which processes audio by applying various frequency-dependent effects like
// low-pass, high-pass, or peak filters.
using Filter = juce::dsp::IIR::Filter<float>;

// A chain of four filters
// Used to construct high-order filters by cascading multiple simple filters
using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter>;

// Consists of a low-cut, a peak, and a high-cut filter
using MonoChain = juce::dsp::ProcessorChain<CutFilter, Filter, CutFilter>;

enum ChainPositions
{
    LowCut,
    Peak,
    HighCut
};

using Coefficients = Filter::CoefficientsPtr;
void updateCoefficients(Coefficients& old, const Coefficients& replacements);

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate);

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
    
    // Parameter management: managing parameters in a structured and automatable way,
    // allowing integrating sliders, knobs, and other UI elements in the plugin editor
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts{*this, nullptr, "Parameters", createParameterLayout()};

private:
    MonoChain leftChain, rightChain;
    
    // Update the peak filter coefficients (frequency, gain, and quality factor)
    // based on user settings stored in ChainSettings
    void updatePeakFilter(const ChainSettings& chainSettings);
    
    // Updates the coefficients for a specific stage in the filter chain.
    // 'Index' determines which filter stage (e.g., stage 0, 1, 2, or 3) to update.
    // 'chain' is the filter chain containing the filter stages.
    // 'cutCoefficients' is an array of coefficient pointers, each corresponding to a filter stage.
    template<int Index, typename ChainType, typename CoefficientType>
    void update(ChainType& chain, const CoefficientType& cutCoefficients)
    {
        // Replace the current coefficients of the filter stage with the new coefficients.
        // This adjusts the filter's behavior (e.g., frequency, slope) for that stage.
        updateCoefficients(chain.template get<Index>().coefficients, cutCoefficients[Index]);
        // Enable the filter stage by setting its 'bypassed' state to 'false'.
        // This ensures the updated filter stage becomes active in the audio processing chain.
        chain.template setBypassed<Index>(false);
    }
    
    template<typename ChainType, typename CoefficientType>
    void updateCutFilter(ChainType& lowCut, const CoefficientType& cutCoefficient, const Slope& lowCutSlope)
    {
        // Reset all filter stages to bypassed
        // This ensures that no stages are active unless explicitly set below.
        lowCut.template setBypassed<0>(true);
        lowCut.template setBypassed<1>(true);
        lowCut.template setBypassed<2>(true);
        lowCut.template setBypassed<3>(true);
        
        switch(lowCutSlope)
        {
            case Slope_48:
            {
                update<3>(lowCut, cutCoefficient);
                break;
            }
            case Slope_36:
            {
                update<2>(lowCut, cutCoefficient);
                break;
            }
            case Slope_24:
            {
                update<1>(lowCut, cutCoefficient);
                break;
            }
            case Slope_12:
            {
                update<0>(lowCut, cutCoefficient);
                break;
            }
        }
    }
    
    void updateLowCutFilters(const ChainSettings& chainSettings);
    void updateHighCutFilters(const ChainSettings& chainSettings);
    void updateFilters();
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (_3BandMultiEffectorAudioProcessor)
};
