/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.
    Responsible for the visual element of the plugin.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

const juce::Colour responseCurveBG = juce::Colour(29, 32, 33);
const juce::Colour responseCurveLine = juce::Colour(219, 208, 171);
const juce::Colour fftLeft = juce::Colour(251, 73, 52);
const juce::Colour fftRight = juce::Colour(235, 219, 178);

const juce::Colour crossoverLeft = juce::Colour(217, 157, 129);
const juce::Colour crossoverMid = juce::Colour(162, 123, 92);
const juce::Colour crossoverRight = juce::Colour(255, 232, 182);

const juce::Colour parameterNameText = juce::Colour(219, 208, 171);
const juce::Colour parameterValueText = juce::Colour(168, 153, 132);

const juce::Colour knob = juce::Colour(219, 208, 171);
const juce::Colour knobOutline = juce::Colour(168, 153, 132);
const juce::Colour knobPointer = juce::Colour(251, 73, 52);

const juce::Colour comboBox = juce::Colour(219, 208, 171);
const juce::Colour comboBoxText = juce::Colour(168, 153, 132);
const juce::Colour popUpMenuHighlight = juce::Colour(168, 153, 132);

const juce::Colour generalBG = juce::Colour(50, 48, 47);

// ====================================== FFT ====================================== //

enum FFTOrder
{
    order2048 = 11,
    order4096 = 12,
    order8192 = 13
};

template<typename BlockType>
struct FFTDataGenerator
{
    /**
     produces the FFT data from an audio buffer.
     */
    void produceFFTDataForRendering(const juce::AudioBuffer<float>& audioData, const float negativeInfinity)
    {
        const auto fftSize = getFFTSize();
        
        fftData.assign(fftData.size(), 0);
        auto* readIndex = audioData.getReadPointer(0);
        std::copy(readIndex, readIndex + fftSize, fftData.begin());
        
        // first apply a windowing function to our data
        window->multiplyWithWindowingTable (fftData.data(), fftSize);       // [1]
        
        // then render our FFT data..
        forwardFFT->performFrequencyOnlyForwardTransform (fftData.data());  // [2]
        
        int numBins = (int)fftSize / 2;
        
        //normalize the fft values.
        for( int i = 0; i < numBins; ++i )
        {
            auto v = fftData[i];
//            fftData[i] /= (float) numBins;
            if( !std::isinf(v) && !std::isnan(v) )
            {
                v /= float(numBins);
            }
            else
            {
                v = 0.f;
            }
            fftData[i] = v;
        }
        
        //convert them to decibels
        for( int i = 0; i < numBins; ++i )
        {
            fftData[i] = juce::Decibels::gainToDecibels(fftData[i], negativeInfinity);
        }
        
        fftDataFifo.push(fftData);
    }
    
    void changeOrder(FFTOrder newOrder)
    {
        //when you change order, recreate the window, forwardFFT, fifo, fftData
        //also reset the fifoIndex
        //things that need recreating should be created on the heap via std::make_unique<>
        order = newOrder;
        auto fftSize = getFFTSize();
        
        forwardFFT = std::make_unique<juce::dsp::FFT>(order);
        window = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris);
        
        fftData.clear();
        fftData.resize(fftSize * 2, 0);

        fftDataFifo.prepare(fftData.size());
    }
    int getFFTSize() const { return 1 << order; }
    int getNumAvailableFFTDataBlocks() const { return fftDataFifo.getNumAvailableForReading(); }
    bool getFFTData(BlockType& fftData) { return fftDataFifo.pull(fftData); }
private:
    FFTOrder order;
    BlockType fftData;
    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;
    
    Fifo<BlockType> fftDataFifo;
};


// ====================================== Path Generator ====================================== //

template<typename PathType>
struct AnalyzerPathGenerator
{
    /*
     converts 'renderData[]' into a juce::Path
     */
    void generatePath(const std::vector<float>& renderData,
                      juce::Rectangle<float> fftBounds,
                      int fftSize,
                      float binWidth,
                      float negativeInfinity)
    {
        auto top = fftBounds.getY();
        auto bottom = fftBounds.getHeight();
        auto width = fftBounds.getWidth();

        int numBins = (int)fftSize / 2;

        PathType p;
        p.preallocateSpace(3 * (int)fftBounds.getWidth());

        auto map = [bottom, top, negativeInfinity](float v)
        {
            return juce::jmap(v,
                              negativeInfinity, 0.f,
                              float(bottom+10),   top);
        };

        auto y = map(renderData[0]);

        if( std::isnan(y) || std::isinf(y) )
            y = bottom;
        
        p.startNewSubPath(0, y);

        const int pathResolution = 2; //you can draw line-to's every 'pathResolution' pixels.

        for( int binNum = 1; binNum < numBins; binNum += pathResolution )
        {
            y = map(renderData[binNum]);

            if( !std::isnan(y) && !std::isinf(y) )
            {
                auto binFreq = binNum * binWidth;
                auto normalizedBinX = juce::mapFromLog10(binFreq, 20.f, 20000.f);
                int binX = std::floor(normalizedBinX * width);
                p.lineTo(binX, y);
            }
        }

        pathFifo.push(p);
    }

    int getNumPathsAvailable() const
    {
        return pathFifo.getNumAvailableForReading();
    }

    bool getPath(PathType& path)
    {
        return pathFifo.pull(path);
    }
private:
    Fifo<PathType> pathFifo;
};

struct PathProducer
{
    PathProducer(SingleChannelSampleFifo<_3BandMultiEffectorAudioProcessor::BlockType>& scsf):
    leftChannelFifo(&scsf)
    {
        leftChannelFFTDataGenerator.changeOrder(FFTOrder::order8192);
        monoBuffer.setSize(1, leftChannelFFTDataGenerator.getFFTSize());
    }
    void process(juce::Rectangle<float> fftBounds, double sampleRate);
    juce::Path getPath() {return leftChannelFFTPath;}
private:
    SingleChannelSampleFifo<_3BandMultiEffectorAudioProcessor::BlockType>* leftChannelFifo;
    
    juce::AudioBuffer<float> monoBuffer;
    
    FFTDataGenerator<std::vector<float>> leftChannelFFTDataGenerator;
    
    AnalyzerPathGenerator<juce::Path> pathProducer;
    
    juce::Path leftChannelFFTPath;
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
    void updateChain();
private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    _3BandMultiEffectorAudioProcessor& audioProcessor;
    juce::Atomic<bool> parametersChanged{false};
    MonoChain monoChain;
    CrossoverFilters crossoverFilters;
    juce::Image background;
    juce::Rectangle<int> getRenderArea();
    
    PathProducer leftPathProducer, rightPathProducer;
};

// ====================================== Custom ComboBox ====================================== //

struct CustomLookAndFeelComboBox: juce::LookAndFeel_V4
{
    CustomLookAndFeelComboBox()
    {
        setColour(juce::ComboBox::backgroundColourId, comboBox);
        setColour(juce::ComboBox::outlineColourId, comboBoxText);
        setColour(juce::ComboBox::arrowColourId, comboBoxText);
        setColour(juce::ComboBox::textColourId, comboBoxText.darker());
        setColour(juce::ComboBox::arrowColourId, comboBoxText.darker());
        
        setColour(juce::PopupMenu::backgroundColourId, comboBox);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, popUpMenuHighlight);
        setColour(juce::PopupMenu::textColourId, comboBoxText.darker());
        setColour(juce::PopupMenu::highlightedTextColourId, generalBG);
    }
};

// ====================================== Custom Button ====================================== //

struct CustomLookAndFeelButton : juce::LookAndFeel_V4
{
    CustomLookAndFeelButton()
    {
        setColour(juce::TextButton::buttonColourId, comboBox);
        setColour(juce::TextButton::buttonOnColourId, comboBox);
        setColour(juce::TextButton::textColourOffId, comboBoxText.darker());
        setColour(juce::TextButton::textColourOnId, comboBoxText.darker());
        setColour(juce::ComboBox::outlineColourId, comboBoxText);
    }
};

// ====================================== Custom Slider ====================================== //

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

// ====================================== Section Divider ====================================== //

class DividerComponent : public juce::Component
{
public:
    DividerComponent() {}
    ~DividerComponent() override {}

    void paint(juce::Graphics& g) override
    {
        // Set the divider color and draw a horizontal line across the component.
        g.setColour(knob.withAlpha(0.2f));
        auto bounds = getLocalBounds().toFloat();
        g.drawLine(bounds.getX() + 20, bounds.getCentreY(), bounds.getRight() - 20, bounds.getCentreY(), 1.5f);
    }
};

// ====================================== Main Editor Class ====================================== //

class _3BandMultiEffectorAudioProcessorEditor  : public juce::AudioProcessorEditor, public juce::Slider::Listener
{
public:
    _3BandMultiEffectorAudioProcessorEditor (_3BandMultiEffectorAudioProcessor&);
    ~_3BandMultiEffectorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void resized() override;
    
private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    _3BandMultiEffectorAudioProcessor& audioProcessor;
    
    juce::ComboBox lowDistortionTypeComboBox;
    juce::ComboBox midDistortionTypeComboBox;
    juce::ComboBox highDistortionTypeComboBox;
    
    CustomLookAndFeelComboBox customLookAndFeelComboBox;
    
    CustomLookAndFeelButton customLookAndFeelButton;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lowDistortionTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midDistortionTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> highDistortionTypeAttachment;
    
    RotarySliderWithLabels peakFreqSlider,
                        peakGainSlider,
                        peakQualitySlider,
                        lowCutFreqSlider,
                        highCutFreqSlider,
                        lowCutSlopeSlider,
                        highCutSlopeSlider,
                        crossoverLowSlider,
                        crossoverHighSlider,
                        lowBandDriveSlider,
                        midBandDriveSlider,
                        highBandDriveSlider,
                        lowBandPostGainSlider,
                        midBandPostGainSlider,
                        highBandPostGainSlider,
                        lowBandMixSlider,
                        midBandMixSlider,
                        highBandMixSlider;
    
    ResponseCurveComponent responseCurveComponent;
    DividerComponent crossoverDivider;
    
    juce::Label levelCompensationLabel;
    juce::TextButton levelCompensationButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelCompensationButtonAttachment;
    
    // Alias to make this extra name looks cleaner
    using APVTS = juce::AudioProcessorValueTreeState;
    using Attachment = APVTS::SliderAttachment;
    
    Attachment peakFreqSliderAttachment,
                peakGainSliderAttachment,
                peakQualitySliderAttachment,
                lowCutFreqSliderAttachment,
                highCutFreqSliderAttachment,
                lowCutSlopeSliderAttachment,
                highCutSlopeSliderAttachment,
                crossoverLowSliderAttachment,
                crossoverHighSliderAttachment,
                lowBandDriveSliderAttachment,
                midBandDriveSliderAttachment,
                highBandDriveSliderAttachment,
                lowBandPostGainSliderAttachment,
                midBandPostGainSliderAttachment,
                highBandPostGainSliderAttachment,
                lowBandMixSliderAttachment,
                midBandMixSliderAttachment,
                highBandMixSliderAttachment;
    
    std::vector<juce::Component*> getComps();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (_3BandMultiEffectorAudioProcessorEditor)
};
