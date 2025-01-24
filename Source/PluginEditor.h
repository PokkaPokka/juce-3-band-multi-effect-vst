/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.
    Responsible for the visual element of the plugin.
 
     Dark Green: (114, 125, 115)
     Light Green: (170, 185, 154)
     Light Blue: (208, 221, 208)
     Creamy White: (240, 240, 215)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

struct LookAndFeel: juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics&,
                                    int x, int y, int width, int height,
                                    float sliderPosProportional,
                                    float rotaryStartAngle,
                                    float rotaryEndAngle,
                           juce::Slider&) override;
};

struct RotarySliderWithLabels: juce::Slider
{
    RotarySliderWithLabels(juce::RangedAudioParameter& rap, const juce::String& unitSuffix): juce::Slider(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag, juce::Slider::TextEntryBoxPosition::NoTextBox),
        param(&rap),
        suffix(unitSuffix)
    {
        setLookAndFeel(&lnf);
        jassert(param != nullptr);
    };
    
    ~RotarySliderWithLabels()
    {
        setLookAndFeel(nullptr);
    }
    
    struct LabelPos
    {
        float pos;
        juce::String label;
    };
    
    juce::Array<LabelPos> labels;
    
    void paint(juce::Graphics& g) override;
    juce::Rectangle<int> getSliderBounds() const;
    int getTextHeight() const {return 14;}
    juce::String getDisplayString() const;
    
    juce::RangedAudioParameter* param;
private:
    LookAndFeel lnf;
    
    juce::String suffix;
};

struct ResponseCurveComponent: juce::Component, juce::AudioProcessorParameter::Listener, juce::Timer
{
    ResponseCurveComponent(_3BandMultiEffectorAudioProcessor&);
    ~ResponseCurveComponent();
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override { }
    void timerCallback() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    _3BandMultiEffectorAudioProcessor& audioProcessor;
    juce::Atomic<bool> parametersChanged{false};
    MonoChain monoChain;
    void updateChain();
    juce::Image background;
    juce::Rectangle<int> getRenderArea();
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
    
    RotarySliderWithLabels peakFreqSlider,
                        peakGainSlider,
                        peakQualitySlider,
                        lowCutFreqSlider,
                        highCutFreqSlider,
                        lowCutSlopeSlider,
                        highCutSlopeSlider;
    
    ResponseCurveComponent responseCurveComponent;
    
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
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (_3BandMultiEffectorAudioProcessorEditor)
};
