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
    CustomRotarySlider(): juce::Slider(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag, juce::Slider::TextEntryBoxPosition::TextBoxBelow)
    {
        
    };
};

struct CustomHorizontalSlider: juce::Slider
{
    CustomHorizontalSlider(): juce::Slider(juce::Slider::SliderStyle::LinearHorizontal,
                                           juce::Slider::TextEntryBoxPosition::TextBoxBelow)
    {
        
    };
};
//==============================================================================
/**
*/
class _3BandMultiEffectorAudioProcessorEditor  : public juce::AudioProcessorEditor,
juce::AudioProcessorParameter::Listener, juce::Timer
{
public:
    _3BandMultiEffectorAudioProcessorEditor (_3BandMultiEffectorAudioProcessor&);
    ~_3BandMultiEffectorAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override{};
    void timerCallback() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    _3BandMultiEffectorAudioProcessor& audioProcessor;

    juce::Atomic<bool> parametersChanged{false};
    
    CustomRotarySlider peakFreqSlider,
                        peakGainSlider,
                        peakQualitySlider,
                        lowCutFreqSlider,
                        highCutFreqSlider;

    CustomHorizontalSlider lowCutSlopeSlider, highCutSlopeSlider;
    
    // Alias to make this extra name looks cleaner
    using APVTS = juce::AudioProcessorValueTreeState;
    using Attachment = APVTS::SliderAttachment;
    
    Attachment peakFreqSliderAttachment,
                peakGainSliderAttachment,
                peakQualitySliderAttachment,
                lowCutFreqSliderAttachment,
                highCutFreqSliderAttachment,
                lowCutSlopeSliderAttachment,
                highCutSlopeSliderAttachment;
    
    std::vector<juce::Component*> getComps();
    
    MonoChain monoChain;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (_3BandMultiEffectorAudioProcessorEditor)
};
