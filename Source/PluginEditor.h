/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.
    Responsible for the visual element of the plugin.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

struct CustomRotarySlider: juce::Slider
{
    CustomRotarySlider(): juce::Slider(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag, juce::Slider::TextEntryBoxPosition::NoTextBox)
    {
        
    };
};

//==============================================================================
/**
*/
class _3BandMultiEffectorAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    _3BandMultiEffectorAudioProcessorEditor (_3BandMultiEffectorAudioProcessor&);
    ~_3BandMultiEffectorAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    _3BandMultiEffectorAudioProcessor& audioProcessor;

    CustomRotarySlider peakFreqSlider, peakGainSlider, peakQualitySlider, lowCutFreqSlider, highCutFreqSlider, lowCutSlopeSlider, highCutSlopeSlider;
    
    std::vector<juce::Component*> getComps();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (_3BandMultiEffectorAudioProcessorEditor)
};
