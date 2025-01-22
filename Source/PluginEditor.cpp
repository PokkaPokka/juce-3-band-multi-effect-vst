/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

void LookAndFeel::drawRotarySlider(juce::Graphics & g, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider & slider)
{
    using namespace juce;
    auto bounds = Rectangle<float>(x, y, width, height);
    
    g.setColour(Colour(240, 240, 215));
    g.fillEllipse(bounds);
    
    g.setColour(Colour(170, 185, 154));
    g.drawEllipse(bounds, 1.f);
    
    if (auto* rswl = dynamic_cast<RotarySliderWithLabels*>(&slider))
    {
        auto center = bounds.getCentre();
        auto angle = jmap(sliderPosProportional, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);
        
        Path valueArc;
        Path minMaxArc;
        Rectangle<float> outerBounds = bounds.reduced(-4.0f); // Slightly larger than the slider
        
        minMaxArc.addCentredArc(center.x, center.y, outerBounds.getWidth() * 0.485f, outerBounds.getHeight() * 0.485f,
                               0.0f, degreesToRadians(-135.f), degreesToRadians(180.f - 45.f), true);
        
        valueArc.addCentredArc(center.x, center.y, outerBounds.getWidth() * 0.485f, outerBounds.getHeight() * 0.485f,
                               0.0f, rotaryStartAngle, angle, true);
        
        g.setColour(Colour(170, 185, 154).brighter());
        g.strokePath(minMaxArc, PathStrokeType(5.f, PathStrokeType::curved, PathStrokeType::butt));
        
        g.setColour(Colour(170, 185, 154));
        g.strokePath(valueArc, PathStrokeType(5.f, PathStrokeType::curved, PathStrokeType::butt));
        
        Path p;
        
        Rectangle<float> r;
        r.setLeft(center.getX() - 1);
        r.setRight(center.getX() + 1);
        r.setTop(bounds.getY());
        r.setBottom(center.getY());
        
        p.addRoundedRectangle(r, 2);

        jassert(rotaryStartAngle < rotaryEndAngle);
        
        auto sliderAngRad = jmap(sliderPosProportional, 0.f, 1.f, rotaryStartAngle, rotaryEndAngle);
        p.applyTransform(AffineTransform().rotated(sliderAngRad, center.getX(), center.getY()));
        
        g.fillPath(p);
        
        g.setFont(rswl->getTextHeight() + 2);
        auto text = rswl->getDisplayString();
        auto strWidth = g.getCurrentFont().getStringWidth(text) + 2;
        
        r.setSize(strWidth + 4, rswl->getTextHeight() + 1);
        r.setCentre(bounds.getCentreX(), bounds.getY() - rswl->getTextHeight() + 1);
        
        g.setColour(Colour(208, 221, 208));
        g.fillRect(r);
        
        g.setColour(Colour(114, 125, 115));
        g.drawFittedText(text, r.toNearestInt(), juce::Justification::centred, 1);
    }
}

//==============================================================================
void RotarySliderWithLabels::paint(juce::Graphics &g)
{
    using namespace juce;
    
    auto startAng = degreesToRadians(180.f + 45.f);
    auto endAng = degreesToRadians(180.f - 45.f) + MathConstants<float>::twoPi;
    
    auto range = getRange();
    auto sliderBounds = getSliderBounds();
    
//    g.setColour(Colours::red);
//    g.drawRect(getLocalBounds());
//    g.setColour(Colours::yellow);
//    g.drawRect(sliderBounds);
//    
    getLookAndFeel().drawRotarySlider(g, sliderBounds.getX(), sliderBounds.getY(), sliderBounds.getWidth(), sliderBounds.getHeight(), jmap(getValue(), range.getStart(), range.getEnd(), 0.0, 1.0), startAng, endAng, *this);
    
    auto center = sliderBounds.toFloat().getCentre();
    auto radius = sliderBounds.getWidth() * 0.5;
    
    g.setColour(Colour(170, 185, 154));
    g.setFont(getTextHeight() - 1);
    
    auto numChoices = labels.size();
    for (int i = 0; i < numChoices; ++i) {
        auto pos = labels[i].pos;
        jassert(0.f <= pos);
        jassert(pos <= 1.f);
        
        auto ang = jmap(pos, 0.0f, 1.0f, startAng, endAng);
        auto centerPoint = center.getPointOnCircumference(radius + getTextHeight() * 0.5f + 1, ang);
        
        Rectangle<float> r;
        auto str = labels[i].label;
        r.setSize(g.getCurrentFont().getStringWidth(str), getTextHeight());
        r.setCentre(centerPoint);
        r.setY(r.getY() + getTextHeight() - 2);
        
        g.drawFittedText(str, r.toNearestInt(), juce::Justification::centred, 1);
    }
}

juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const
{
    auto bounds = getLocalBounds();
    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 15;
    
    size -= getTextHeight() * 2;
    juce::Rectangle<int> r;
    r.setSize(size, size);
    r.setCentre(bounds.getCentreX(), 0);
    r.setY(27);
    
    return r;
}

juce::String RotarySliderWithLabels::getDisplayString() const
{
    juce::String str;
    
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param)) {
        return choiceParam->getCurrentChoiceName();
    } else {
        str << getValue();
        if (suffix.isNotEmpty()) {
            str << " " << suffix;
        }
        return str;
    }
}

//==============================================================================
ResponseCurveComponent::ResponseCurveComponent(_3BandMultiEffectorAudioProcessor& p): audioProcessor(p)
{
    const auto& params = audioProcessor.getParameters();
    for (auto param: params) {
        param->addListener(this);
    }
    
    parametersChanged.set(true);
    startTimerHz(60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    const auto& params = audioProcessor.getParameters();
    for (auto param: params) {
        param->removeListener(this);
    }
}

void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue)
{
    parametersChanged.set(true);
}

void ResponseCurveComponent::timerCallback()
{
    if (parametersChanged.compareAndSetBool(false, true)) {
        auto chainSettings = getChainSettings(audioProcessor.apvts);
        auto peakCoefficients = makePeakFilter(chainSettings, audioProcessor.getSampleRate());
        updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
        
        auto lowCutCoefficients = makeLowCutFilter(chainSettings, audioProcessor.getSampleRate());
        auto highCutCoefficients = makeHighCutFilter(chainSettings, audioProcessor.getSampleRate());
        
        updateCutFilter(monoChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
        updateCutFilter(monoChain.get<ChainPositions::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);
        
        repaint();
    }
}

void ResponseCurveComponent::paint (juce::Graphics& g)
{
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll(Colour(114, 125, 115).darker());
    
    auto bounds = getLocalBounds();
    auto responseArea = bounds;
    auto w = responseArea.getWidth();
    
    auto& lowcut = monoChain.get<ChainPositions::LowCut>();
    auto& peak = monoChain.get<ChainPositions::Peak>();
    auto& highcut = monoChain.get<ChainPositions::HighCut>();
    
    auto sampleRate = audioProcessor.getSampleRate();
    
    std::vector<double> mags;
    mags.resize(w);
    for (int i = 0; i < w; ++i) {
        double mag = 1.f;
        auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);
        if (!monoChain.isBypassed<ChainPositions::Peak>()) {
            mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!lowcut.isBypassed<0>()) {
            mag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!lowcut.isBypassed<1>()) {
            mag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!lowcut.isBypassed<2>()) {
            mag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!lowcut.isBypassed<3>()) {
            mag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!highcut.isBypassed<0>()) {
            mag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!highcut.isBypassed<1>()) {
            mag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!highcut.isBypassed<2>()) {
            mag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!highcut.isBypassed<3>()) {
            mag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        
        mags[i] = Decibels::gainToDecibels(mag);
    }
    
    Path responseCurve;
    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();
    auto map = [outputMin, outputMax](double input) {
        return jmap(input, -24.0, 24.0, outputMin, outputMax);
    };
    
    responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));
    
    for (size_t i = 1; i < mags.size(); ++i) {
        responseCurve.lineTo(responseArea.getX() + i, map((mags[i])));
    }
    
    g.setColour(Colour(114, 125, 115));
    g.drawRoundedRectangle(responseArea.toFloat(), 4.f, 1.f);
    
    DropShadow shadow(Colour(114, 125, 115), 28, Point<int>(0, 0));
    shadow.drawForRectangle(g, responseArea);
    
    g.setColour(Colour(240, 240, 215));
    g.strokePath(responseCurve, PathStrokeType(2.f));
}

//==============================================================================
_3BandMultiEffectorAudioProcessorEditor::
_3BandMultiEffectorAudioProcessorEditor (_3BandMultiEffectorAudioProcessor& p)
: AudioProcessorEditor (&p), audioProcessor (p),
peakFreqSlider(*audioProcessor.apvts.getParameter("Peak Frequency"), "Hz"),
peakGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "dB"),
peakQualitySlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
lowCutFreqSlider(*audioProcessor.apvts.getParameter("Low-Cut Frequency"), "Hz"),
highCutFreqSlider(*audioProcessor.apvts.getParameter("High-Cut Frequency"), "Hz"),
lowCutSlopeSlider(*audioProcessor.apvts.getParameter("Low-Cut Slope"), "dB/Oct"),
highCutSlopeSlider(*audioProcessor.apvts.getParameter("High-Cut Slope"), "dB/Oct"),

responseCurveComponent(audioProcessor),
peakFreqSliderAttachment(audioProcessor.apvts, "Peak Frequency", peakFreqSlider),
peakGainSliderAttachment(audioProcessor.apvts, "Peak Gain", peakGainSlider),
peakQualitySliderAttachment(audioProcessor.apvts, "Peak Quality", peakQualitySlider),
lowCutFreqSliderAttachment(audioProcessor.apvts, "Low-Cut Frequency", lowCutFreqSlider),
highCutFreqSliderAttachment(audioProcessor.apvts, "High-Cut Frequency", highCutFreqSlider),
lowCutSlopeSliderAttachment(audioProcessor.apvts,"Low-Cut Slope", lowCutSlopeSlider),
highCutSlopeSliderAttachment(audioProcessor.apvts, "High-Cut Slope", highCutSlopeSlider)
{
    // Labels for Peak Frequency Slider
    peakFreqSlider.labels.add({0.f, "20"});
    peakFreqSlider.labels.add({1.f, "20k"});

    // Labels for Peak Gain Slider
    peakGainSlider.labels.add({0.f, "-24"});
    peakGainSlider.labels.add({1.f, "24"});

    // Labels for Peak Quality Slider
    peakQualitySlider.labels.add({0.f, "0.1"});
    peakQualitySlider.labels.add({1.f, "10"});

    // Labels for Low-Cut Frequency Slider
    lowCutFreqSlider.labels.add({0.f, "20"});
    lowCutFreqSlider.labels.add({1.f, "20k"});

    // Labels for High-Cut Frequency Slider
    highCutFreqSlider.labels.add({0.f, "20"});
    highCutFreqSlider.labels.add({1.f, "20k"});

    // Labels for Low-Cut Slope Slider
    lowCutSlopeSlider.labels.add({0.f, "12"});
    lowCutSlopeSlider.labels.add({1.f, "48"});

    // Labels for High-Cut Slope Slider
    highCutSlopeSlider.labels.add({0.f, "12"});
    highCutSlopeSlider.labels.add({1.f, "48"});
    
    for (auto* comp: getComps())
    {
        addAndMakeVisible(comp);
    }
    
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (600, 400);
}

_3BandMultiEffectorAudioProcessorEditor::~_3BandMultiEffectorAudioProcessorEditor()
{
    
}

//==============================================================================
void _3BandMultiEffectorAudioProcessorEditor::paint(juce::Graphics& g)
{
    using namespace juce;
    g.fillAll (Colour(208, 221, 208));
}

void _3BandMultiEffectorAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    auto bounds = getLocalBounds();
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * 0.33);
    
    responseCurveComponent.setBounds(responseArea);
    
    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);
    
    lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
    highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
    
    lowCutSlopeSlider.setBounds(lowCutArea);
    highCutSlopeSlider.setBounds(highCutArea);
    lowCutSlopeSlider.setTextBoxIsEditable(false);
    highCutSlopeSlider.setTextBoxIsEditable(false);
    
    peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight () * 0.33));
    peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight () * 0.5));
    peakQualitySlider.setBounds(bounds);
}

std::vector<juce::Component*> _3BandMultiEffectorAudioProcessorEditor::getComps()
{
    return
    {
        &peakFreqSlider,
        &peakGainSlider,
        &peakQualitySlider,
        &lowCutFreqSlider,
        &highCutFreqSlider,
        &lowCutSlopeSlider,
        &highCutSlopeSlider,
        &responseCurveComponent
    };
}
