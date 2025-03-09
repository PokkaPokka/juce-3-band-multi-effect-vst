/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ====================================== Response Curve ====================================== //

ResponseCurveComponent::ResponseCurveComponent(_3BandMultiEffectorAudioProcessor& p): audioProcessor(p),
leftPathProducer(audioProcessor.leftChannelFifo),
rightPathProducer(audioProcessor.rightChannelFifo)
{
    const auto& params = audioProcessor.getParameters();
    for (auto param: params) {
        param->addListener(this);
    }
    
    updateChain();
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

void PathProducer::process(juce::Rectangle<float> fftBounds, double sampleRate)
{
    juce::AudioBuffer<float> tempIncomingBuffer;
    
    while(leftChannelFifo->getNumCompleteBuffersAvailable() > 0) {
        if (leftChannelFifo->getAudioBuffer(tempIncomingBuffer)) {
            auto size = tempIncomingBuffer.getNumSamples();
            juce::FloatVectorOperations::copy(monoBuffer.getWritePointer(0, 0),
                                              monoBuffer.getReadPointer(0, size),
                                              monoBuffer.getNumSamples() - size);
            
            juce::FloatVectorOperations::copy(monoBuffer.getWritePointer(0, monoBuffer.getNumSamples() - size),
                                              tempIncomingBuffer.getReadPointer(0, 0),
                                              size);
            
            leftChannelFFTDataGenerator.produceFFTDataForRendering(monoBuffer, -48.f);
        }
    }

    const auto fftSize = leftChannelFFTDataGenerator.getFFTSize();
    const auto binWidth = sampleRate / (double)fftSize;
    
    while (leftChannelFFTDataGenerator.getNumAvailableFFTDataBlocks() > 0) {
        std::vector<float> fftData;
        if (leftChannelFFTDataGenerator.getFFTData(fftData)) {
            pathProducer.generatePath(fftData, fftBounds, fftSize, binWidth, -48.f);
        }
    }
    
    while (pathProducer.getNumPathsAvailable()) {
        pathProducer.getPath(leftChannelFFTPath);
    }
}

void ResponseCurveComponent::timerCallback()
{
    
    auto fftBounds = getRenderArea().toFloat();
    auto sampleRate = audioProcessor.getSampleRate();
    
    leftPathProducer.process(fftBounds, sampleRate);
    rightPathProducer.process(fftBounds, sampleRate);
    
    if (parametersChanged.compareAndSetBool(false, true)) {
        updateChain();
    }
    
    repaint();
}

void ResponseCurveComponent::updateChain()
{
    auto chainSettings = getChainSettings(audioProcessor.apvts);
    auto peakCoefficients = makePeakFilter(chainSettings, audioProcessor.getSampleRate());
    updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    
    auto lowCutCoefficients = makeLowCutFilter(chainSettings, audioProcessor.getSampleRate());
    auto highCutCoefficients = makeHighCutFilter(chainSettings, audioProcessor.getSampleRate());
    
    updateCutFilter(monoChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
    updateCutFilter(monoChain.get<ChainPositions::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);
    
    crossoverFilters.update(chainSettings.crossoverLow, chainSettings.crossoverHigh);
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
    using namespace juce;

    auto bounds = getLocalBounds();
    g.fillAll(responseCurveBG.darker());

    // Inner shadow effect
    DropShadow shadow(responseCurveBG, 28, Point<int>(0, 0));
    Image shadowImage(Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
    Graphics shadowGraphics(shadowImage);
    shadowGraphics.setColour(Colours::transparentWhite);
    shadowGraphics.fillAll();
    shadow.drawForRectangle(shadowGraphics, bounds);
    g.setOpacity(0.9f);
    g.drawImageAt(shadowImage, bounds.getX(), bounds.getY());

    g.drawImage(background, getLocalBounds().toFloat());

    auto responseArea = getRenderArea();
    auto w = responseArea.getWidth();

    // Existing filter magnitude calculation (unchanged)
    auto& lowcut = monoChain.get<ChainPositions::LowCut>();
    auto& peak = monoChain.get<ChainPositions::Peak>();
    auto& highcut = monoChain.get<ChainPositions::HighCut>();
    auto& lowBandLine = crossoverFilters.getCutoffFrequencies()[0];
    auto& highBandLine = crossoverFilters.getCutoffFrequencies()[1];
    auto sampleRate = audioProcessor.getSampleRate();

    std::vector<double> mags;
    mags.resize(w);
    for (int i = 0; i < w; ++i) {
        double mag = 1.f;
        auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);
        if (!monoChain.isBypassed<ChainPositions::Peak>()) {
            mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        // (Rest of magnitude calculation remains unchanged)
        if (!lowcut.isBypassed<0>()) mag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<1>()) mag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<2>()) mag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<3>()) mag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<0>()) mag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<1>()) mag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<2>()) mag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<3>()) mag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        mags[i] = Decibels::gainToDecibels(mag);
    }

    // Draw the response curve (unchanged)
    Path responseCurve;
    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();
    auto map = [outputMin, outputMax](double input) {
        return jmap(input, -24.0, 24.0, outputMin, outputMax);
    };
    responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));
    for (size_t i = 1; i < mags.size(); ++i) {
        responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));
    }

    // Draw FFT paths
    auto leftChannelFFTPath = leftPathProducer.getPath();
    auto rightChannelFFTPath = rightPathProducer.getPath();
    g.setColour(fftLeft.withAlpha(0.7f));
    g.strokePath(leftChannelFFTPath, PathStrokeType(1.f));
    g.setColour(fftRight.withAlpha(0.7f));
    g.strokePath(rightChannelFFTPath, PathStrokeType(1.f));

    // Draw the response curve
    g.setColour(responseCurveLine);
    g.strokePath(responseCurve, PathStrokeType(2.f));

    // *** Draw crossover lines ***
    auto mapFreqToX = [&](float freq) {
        auto normX = mapFromLog10(freq, 20.f, 20000.f); // Map frequency to normalized X (0 to 1)
        return responseArea.getX() + normX * w;         // Scale to response area width
    };

    // Draw low crossover area
    float lowX = mapFreqToX(lowBandLine);
    g.setColour(responseCurveLine.withAlpha(0.6f));
    g.fillRect(Rectangle<float> (lowX, 0, 2.0f, responseArea.getBottom() * 1.1));
    g.setColour(crossoverLeft.withAlpha(0.15f));
    g.fillRect(Rectangle<float> (0, 0, lowX, responseArea.getBottom() * 1.1));
    
    
    // Draw high crossover area
    float highX = mapFreqToX(highBandLine);
    g.setColour(responseCurveLine.withAlpha(0.6f));
    g.fillRect(Rectangle<float> (highX, 0, 2.0f, responseArea.getBottom() * 1.1));
    g.setColour(crossoverRight.withAlpha(0.15f));
    g.fillRect(Rectangle<float> (highX, 0, getRenderArea().getWidth() - highX, responseArea.getBottom() * 1.1));
    
    // Draw mid crossover area
    g.setColour(crossoverMid.withAlpha(0.15f));
    g.fillRect(Rectangle<float> (0, 0, highX, responseArea.getBottom() * 1.1));
}

void ResponseCurveComponent::resized()
{
    using namespace juce;
    background = Image(Image::PixelFormat::RGB, getWidth(), getHeight(), true);
    
    Graphics g(background);
    Array<float> freqs {
        20, 30, 40, 50, 100,
        200, 300, 400, 500, 1000,
        2000, 3000, 4000, 5000, 10000,
        20000
    };
    
    g.setColour(responseCurveLine.darker().withAlpha(0.3f));
    for (auto f: freqs) {
        auto normX = mapFromLog10(f, 20.f, 20000.f);
        g.drawVerticalLine(getWidth() * normX, 0.f, getHeight());
    }
    
    Array<float> gain {
        -24, -12, 0, 12, 24
    };
    
    for (auto gDb: gain) {
        auto y = jmap(gDb, -24.f, 24.f, float(getHeight()), 0.f);
        g.drawHorizontalLine(y, 0, getWidth());
    }
}

juce::Rectangle<int> ResponseCurveComponent::getRenderArea()
{
    auto bounds = getLocalBounds();
    
    bounds.reduce(0, 5);
    return bounds;
}

// ====================================== Rotary Slider ====================================== //

// LookAndFeel class for consistent UI styling
void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                  float sliderPosProportional, float rotaryStartAngle,
                                  float rotaryEndAngle, juce::Slider& slider)
{
    using namespace juce;
    auto bounds = Rectangle<float>(x, y, width, height);
    auto center = bounds.getCentre();
    
    // Draw shadow for the knob
    Path knobPath;
    knobPath.addEllipse(bounds);
    DropShadow shadow(Colours::black.withAlpha(0.5f), 10, Point<int>(2, 2));
    shadow.drawForPath(g, knobPath);

    if (auto* rswl = dynamic_cast<RotarySliderWithLabels*>(&slider))
    {
        // Common setup
        auto paramID = rswl->param->getParameterID();
        bool isPeakParam = (paramID == "Peak Frequency" || paramID == "Peak Gain" || paramID == "Peak Quality");
        float arcThickness = isPeakParam ? 4.f : 5.f;
        auto outerBounds = bounds.reduced(isPeakParam ? -3.f : -4.f);
        float radius = outerBounds.getWidth() * 0.485f;
        
        Path valuePath;
        valuePath.addEllipse(outerBounds);
        DropShadow shadow(Colours::black.withAlpha(0.6f), 15, Point<int>(2, 2));
        shadow.drawForPath(g, valuePath);
        
        // Draw base ellipse
        g.setColour(knob);
        g.fillEllipse(bounds);
        g.setColour(knobOutline);
        g.drawEllipse(bounds, 1.f);

        // Draw arcs
        Path minMaxArc;
        minMaxArc.addCentredArc(center.x, center.y, radius, radius, 0.0f,
                               degreesToRadians(-135.f), degreesToRadians(135.f), true);
        
        Path valueArc;
        auto angle = jmap(sliderPosProportional, 0.f, 1.f, rotaryStartAngle, rotaryEndAngle);
        valueArc.addCentredArc(center.x, center.y, radius, radius, 0.0f,
                              rotaryStartAngle, angle, true);

        g.setColour(knobOutline);
        g.strokePath(minMaxArc, PathStrokeType(arcThickness, PathStrokeType::curved, PathStrokeType::butt));
        g.setColour(knobPointer);
        g.strokePath(valueArc, PathStrokeType(arcThickness, PathStrokeType::curved, PathStrokeType::butt));

        // Draw pointer
        Path p;
        Rectangle<float> r(center.getX() - 1, bounds.getY(), 2, center.getY() - bounds.getY());
        p.addRoundedRectangle(r, 2);
        p.applyTransform(AffineTransform().rotated(angle, center.getX(), center.getY()));
        g.fillPath(p);

        // Draw parameter name (if applicable)
        if (rswl->param)
        {
            auto paramName = rswl->param->name;
            g.setFont(rswl->getTextHeight() + (isPeakParam ? -2 : -1));
            auto textWidth = g.getCurrentFont().getStringWidth(paramName);
            Rectangle<float> paramNameBounds(textWidth + 6, rswl->getTextHeight() + 2);
            paramNameBounds.setCentre(center.getX(), bounds.getY() - rswl->getTextHeight() * 2);

            g.setColour(generalBG);
            g.fillRect(paramNameBounds);
            g.setColour(parameterNameText);
            g.drawFittedText(paramName, paramNameBounds.toNearestInt(), Justification::centred, 1);
        }

        // Draw value text
        g.setFont(rswl->getTextHeight() + (isPeakParam ? 0 : 1));
        auto text = rswl->getDisplayString();
        auto strWidth = g.getCurrentFont().getStringWidth(text);
        r.setSize(strWidth + (isPeakParam ? 4 : 6), rswl->getTextHeight() + (isPeakParam ? -1 : 0));
        r.setCentre(center.getX(), bounds.getY() - rswl->getTextHeight() + 1);

        g.setColour(generalBG);
        g.fillRect(r);
        g.setColour(parameterValueText);
        g.drawFittedText(text, r.toNearestInt(), Justification::centred, 1);
    }
}

// Paint the rotary sliders
void RotarySliderWithLabels::paint(juce::Graphics &g)
{
    using namespace juce;
    
    auto startAng = degreesToRadians(180.f + 45.f);
    auto endAng = degreesToRadians(180.f - 45.f) + MathConstants<float>::twoPi;
    
    auto range = getRange();
    auto sliderBounds = getSliderBounds();
   
    getLookAndFeel().drawRotarySlider(g, sliderBounds.getX(), sliderBounds.getY(), sliderBounds.getWidth(), sliderBounds.getHeight(), jmap(getValue(), range.getStart(), range.getEnd(), 0.0, 1.0), startAng, endAng, *this);
    
    auto center = sliderBounds.toFloat().getCentre();
    auto radius = sliderBounds.getWidth() * 0.5;
    
    g.setColour(parameterNameText);
    
    auto numChoices = labels.size();
    for (int i = 0; i < numChoices; ++i) {
        g.setFont(param != nullptr && (param->getParameterID() == "Peak Frequency" ||
                                       param->getParameterID() == "Peak Gain" ||
                                       param->getParameterID() == "Peak Quality")
                      ? getTextHeight() - 3
                      : getTextHeight() - 1);

        auto pos = labels[i].pos;
        jassert(0.f <= pos);
        jassert(pos <= 1.f);

        auto ang = jmap(pos, 0.0f, 1.0f, startAng, endAng);
        auto centerPoint = center.getPointOnCircumference(radius + getTextHeight() * 0.5f + 1, ang);

        Rectangle<float> r;
        auto str = labels[i].label;

        r.setSize(g.getCurrentFont().getStringWidth(str), getTextHeight());
        r.setCentre(centerPoint);
        r.setY(r.getY() + getTextHeight() - (param != nullptr &&
                                              (param->getParameterID() == "Peak Frequency" ||
                                               param->getParameterID() == "Peak Gain" ||
                                               param->getParameterID() == "Peak Quality")
                                              ? 7
                                              : 5));

        g.drawFittedText(str, r.toNearestInt(), juce::Justification::centred, 1);    }
}

juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const
{
    auto bounds = getLocalBounds();
    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 35;
    
    size -= getTextHeight() * 2;
    juce::Rectangle<int> r;
    r.setSize(size, size);
    r.setCentre(bounds.getCentreX(), 0);
    r.setY(48);
    
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

void _3BandMultiEffectorAudioProcessorEditor::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &crossoverLowSlider)
    {
        if (crossoverLowSlider.getValue() > crossoverHighSlider.getValue()) {
            crossoverHighSlider.setValue(crossoverLowSlider.getValue(), juce::sendNotificationSync);
            responseCurveComponent.updateChain();
            responseCurveComponent.repaint();
        }
    }
    else if (slider == &crossoverHighSlider)
    {
        if (crossoverHighSlider.getValue() < crossoverLowSlider.getValue()) {
            crossoverLowSlider.setValue(crossoverHighSlider.getValue(), juce::sendNotificationSync);
            responseCurveComponent.updateChain();
            responseCurveComponent.repaint();
        }
    }
}

// ====================================== Combo Box ====================================== //

void setDistortionComboBoxBounds(juce::Rectangle<int> bounds, int comboBoxHeight,
                                 juce::ComboBox& lowCombo, juce::ComboBox& midCombo, juce::ComboBox& highCombo)
{
    bounds.setHeight(comboBoxHeight);
    bounds.setTop(bounds.getY() + 10);

    // Reduce the total width allocated to combo boxes
    int totalComboBoxWidth = bounds.getWidth() * 0.75f;
    int comboBoxWidth = totalComboBoxWidth / 3;  // Divide equally for three combo boxes
    int spacing = (bounds.getWidth() - totalComboBoxWidth) / 4;  // Distribute space

    // Position each combo box with spacing
    lowCombo.setBounds(bounds.getX() + spacing, bounds.getY(), comboBoxWidth, comboBoxHeight);
    midCombo.setBounds(lowCombo.getRight() + spacing, bounds.getY(), comboBoxWidth, comboBoxHeight);
    highCombo.setBounds(midCombo.getRight() + spacing, bounds.getY(), comboBoxWidth, comboBoxHeight);
}

_3BandMultiEffectorAudioProcessorEditor::~_3BandMultiEffectorAudioProcessorEditor()
{
    lowDistortionTypeComboBox.setLookAndFeel(nullptr);
    midDistortionTypeComboBox.setLookAndFeel(nullptr);
    highDistortionTypeComboBox.setLookAndFeel(nullptr);
    
    levelCompensationButton.setLookAndFeel(nullptr);
}

// ====================================== Processor Editor ====================================== //

_3BandMultiEffectorAudioProcessorEditor::_3BandMultiEffectorAudioProcessorEditor(_3BandMultiEffectorAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
      peakFreqSlider(*audioProcessor.apvts.getParameter("Peak Frequency"), "Hz"),
      peakGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "dB"),
      peakQualitySlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
      lowCutFreqSlider(*audioProcessor.apvts.getParameter("Low-Cut Frequency"), "Hz"),
      highCutFreqSlider(*audioProcessor.apvts.getParameter("High-Cut Frequency"), "Hz"),
      lowCutSlopeSlider(*audioProcessor.apvts.getParameter("Low-Cut Slope"), "dB/Oct"),
      highCutSlopeSlider(*audioProcessor.apvts.getParameter("High-Cut Slope"), "dB/Oct"),
      crossoverLowSlider(*audioProcessor.apvts.getParameter("CrossoverLow"), "Hz"),
      crossoverHighSlider(*audioProcessor.apvts.getParameter("CrossoverHigh"), "Hz"),
      lowBandDriveSlider(*audioProcessor.apvts.getParameter("LowBandDrive"), ""),
      midBandDriveSlider(*audioProcessor.apvts.getParameter("MidBandDrive"), ""),
      highBandDriveSlider(*audioProcessor.apvts.getParameter("HighBandDrive"), ""),
      lowBandPostGainSlider(*audioProcessor.apvts.getParameter("LowBandPostGain"), "dB"),
      midBandPostGainSlider(*audioProcessor.apvts.getParameter("MidBandPostGain"), "dB"),
      highBandPostGainSlider(*audioProcessor.apvts.getParameter("HighBandPostGain"), "dB"),
      lowBandMixSlider(*audioProcessor.apvts.getParameter("LowBandMix"), "%"),
      midBandMixSlider(*audioProcessor.apvts.getParameter("MidBandMix"), "%"),
      highBandMixSlider(*audioProcessor.apvts.getParameter("HighBandMix"), "%"),
      responseCurveComponent(audioProcessor),
      peakFreqSliderAttachment(audioProcessor.apvts, "Peak Frequency", peakFreqSlider),
      peakGainSliderAttachment(audioProcessor.apvts, "Peak Gain", peakGainSlider),
      peakQualitySliderAttachment(audioProcessor.apvts, "Peak Quality", peakQualitySlider),
      lowCutFreqSliderAttachment(audioProcessor.apvts, "Low-Cut Frequency", lowCutFreqSlider),
      highCutFreqSliderAttachment(audioProcessor.apvts, "High-Cut Frequency", highCutFreqSlider),
      lowCutSlopeSliderAttachment(audioProcessor.apvts, "Low-Cut Slope", lowCutSlopeSlider),
      highCutSlopeSliderAttachment(audioProcessor.apvts, "High-Cut Slope", highCutSlopeSlider),
      crossoverLowSliderAttachment(audioProcessor.apvts, "CrossoverLow", crossoverLowSlider),
      crossoverHighSliderAttachment(audioProcessor.apvts, "CrossoverHigh", crossoverHighSlider),
      lowBandDriveSliderAttachment(audioProcessor.apvts, "LowBandDrive", lowBandDriveSlider),
      midBandDriveSliderAttachment(audioProcessor.apvts, "MidBandDrive", midBandDriveSlider),
      highBandDriveSliderAttachment(audioProcessor.apvts, "HighBandDrive", highBandDriveSlider),
      lowBandPostGainSliderAttachment(audioProcessor.apvts, "LowBandPostGain", lowBandPostGainSlider),
      midBandPostGainSliderAttachment(audioProcessor.apvts, "MidBandPostGain", midBandPostGainSlider),
      highBandPostGainSliderAttachment(audioProcessor.apvts, "HighBandPostGain", highBandPostGainSlider),
      lowBandMixSliderAttachment(audioProcessor.apvts, "LowBandMix", lowBandMixSlider),
      midBandMixSliderAttachment(audioProcessor.apvts, "MidBandMix", midBandMixSlider),
      highBandMixSliderAttachment(audioProcessor.apvts, "HighBandMix", highBandMixSlider)
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

    // Labels for Crossover Low Slider
    crossoverLowSlider.labels.add({0.f, "20"});
    crossoverLowSlider.labels.add({1.f, "5k"});

    // Labels for Crossover High Slider
    crossoverHighSlider.labels.add({0.f, "5k"});
    crossoverHighSlider.labels.add({1.f, "20k"});

    // Labels for Low Band Drive Slider
    lowBandDriveSlider.labels.add({0.f, "0"});
    lowBandDriveSlider.labels.add({1.f, "50"});

    // Labels for Mid Band Drive Slider
    midBandDriveSlider.labels.add({0.f, "0"});
    midBandDriveSlider.labels.add({1.f, "50"});

    // Labels for High Band Drive Slider
    highBandDriveSlider.labels.add({0.f, "0"});
    highBandDriveSlider.labels.add({1.f, "50"});

    // Labels for Low Band Post-Gain Slider
    lowBandPostGainSlider.labels.add({0.f, "-40"});
    lowBandPostGainSlider.labels.add({1.f, "20"});

    // Labels for Mid Band Post-Gain Slider
    midBandPostGainSlider.labels.add({0.f, "-40"});
    midBandPostGainSlider.labels.add({1.f, "20"});

    // Labels for High Band Post-Gain Slider
    highBandPostGainSlider.labels.add({0.f, "-40"});
    highBandPostGainSlider.labels.add({1.f, "20"});

    // Labels for Low Band Mix Slider
    lowBandMixSlider.labels.add({0.f, "0"});
    lowBandMixSlider.labels.add({1.f, "100"});

    // Labels for Mid Band Mix Slider
    midBandMixSlider.labels.add({0.f, "0"});
    midBandMixSlider.labels.add({1.f, "100"});

    // Labels for High Band Mix Slider
    highBandMixSlider.labels.add({0.f, "0"});
    highBandMixSlider.labels.add({1.f, "100"});

    // Add distortion type options to combo boxes
    lowDistortionTypeComboBox.addItem("Soft Clipping", 1);
    lowDistortionTypeComboBox.addItem("Hard Clipping", 2);
    lowDistortionTypeComboBox.addItem("ArcTan Distortion", 3);
    lowDistortionTypeComboBox.addItem("Bit Crusher", 4);
    lowDistortionTypeComboBox.addItem("Sine Folding", 5);
    
    midDistortionTypeComboBox.addItem("Soft Clipping", 1);
    midDistortionTypeComboBox.addItem("Hard Clipping", 2);
    midDistortionTypeComboBox.addItem("ArcTan Distortion", 3);
    midDistortionTypeComboBox.addItem("Bit Crusher", 4);
    midDistortionTypeComboBox.addItem("Sine Folding", 5);

    highDistortionTypeComboBox.addItem("Soft Clipping", 1);
    highDistortionTypeComboBox.addItem("Hard Clipping", 2);
    highDistortionTypeComboBox.addItem("ArcTan Distortion", 3);
    highDistortionTypeComboBox.addItem("Bit Crusher", 4);
    highDistortionTypeComboBox.addItem("Sine Folding", 5);

    levelCompensationButton.setClickingTogglesState(true); // Enable toggle behavior
        levelCompensationButton.setButtonText("ON"); // Initial text
        levelCompensationButton.onStateChange = [this]() {
            levelCompensationButton.setButtonText(levelCompensationButton.getToggleState() ? "ON" : "OFF");
        };
    
    levelCompensationLabel.setText("Level\nCompensation", juce::dontSendNotification);
    levelCompensationLabel.setJustificationType(juce::Justification::centredTop);
    levelCompensationLabel.setColour(juce::Label::textColourId, parameterNameText);
    levelCompensationLabel.setFont(juce::Font(14.0f));
    levelCompensationLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(levelCompensationLabel);
    
    levelCompensationButton.setLookAndFeel(&customLookAndFeelButton);
    levelCompensationButtonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.apvts, "LevelCompensation", levelCompensationButton);
    
    // Set custom look and feel for combo boxes
    lowDistortionTypeComboBox.setLookAndFeel(&customLookAndFeelComboBox);
    midDistortionTypeComboBox.setLookAndFeel(&customLookAndFeelComboBox);
    highDistortionTypeComboBox.setLookAndFeel(&customLookAndFeelComboBox);
    
    // Create ComboBoxAttachments to bind combo boxes to parameters
    lowDistortionTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "LowBandType", lowDistortionTypeComboBox);
    midDistortionTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "MidBandType", midDistortionTypeComboBox);
    highDistortionTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "HighBandType", highDistortionTypeComboBox);
    
    crossoverLowSlider.addListener(this);
    crossoverHighSlider.addListener(this);

    // Add all components to the editor
    for (auto* comp : getComps())
    {
        addAndMakeVisible(comp);
    }
    
    addAndMakeVisible(crossoverDivider);

    // Set the editor's size
    setSize(500, 850);
}

void _3BandMultiEffectorAudioProcessorEditor::paint(juce::Graphics& g)
{
    using namespace juce;
    g.fillAll (generalBG);
}

void _3BandMultiEffectorAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    auto bounds = getLocalBounds();
    float hRatio = 13 / 100.f;
    
    // Reserve the top area for the response curve
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * hRatio);
    responseCurveComponent.setBounds(responseArea);
    
    // Reserve some space at the bottom for new sliders
    const int bottomMargin = bounds.getHeight() * 0.65;
    bounds.removeFromBottom(bottomMargin);
    
    // Layout the low cut and high cut areas
    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);
    
    lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
    highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
    
    lowCutSlopeSlider.setBounds(lowCutArea);
    highCutSlopeSlider.setBounds(highCutArea);
    lowCutSlopeSlider.setTextBoxIsEditable(false);
    highCutSlopeSlider.setTextBoxIsEditable(false);
    
    // Layout the peak controls
    peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
    peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
    peakQualitySlider.setBounds(bounds);
    
    auto gap = 25;
    
    auto dividerArea = getLocalBounds();
    dividerArea.setTop(bounds.getBottom());
    crossoverDivider.setBounds(dividerArea.removeFromTop(gap));
    
    // Layout the crossover sliders
    auto crossoverArea = getLocalBounds();
    crossoverArea.setTop(bounds.getBottom() + gap / 2);
    
    crossoverArea.setHeight(bounds.getHeight() * 1.3);
    crossoverLowSlider.setBounds(crossoverArea.removeFromLeft(crossoverArea.getWidth() * 0.33));
    
    auto buttonLabelArea = crossoverArea.removeFromLeft(crossoverArea.getWidth() * 0.5);
    buttonLabelArea.setTop(crossoverArea.getCentreY() - 30);
    buttonLabelArea.setHeight(30);
    buttonLabelArea.setLeft(buttonLabelArea.getCentreX() - 75);
    buttonLabelArea.setWidth(150);
    levelCompensationLabel.setBounds(buttonLabelArea);
    
    auto buttonArea = buttonLabelArea;
    buttonArea.setTop(buttonLabelArea.getBottom() + 10);
    buttonArea.setHeight(25);
    buttonArea.setLeft(buttonArea.getCentreX() - 20);
    buttonArea.setWidth(40);
    levelCompensationButton.setBounds(buttonArea);
    
    crossoverHighSlider.setBounds(crossoverArea);
    
    // Define height for the ComboBox
    int comboBoxHeight = 25;

    // Get the distortion bounds and set its starting position
    auto distortionBounds = getLocalBounds();
    distortionBounds.setTop(crossoverArea.getBottom() + 10);
    distortionBounds.setHeight(comboBoxHeight);

    setDistortionComboBoxBounds(distortionBounds, comboBoxHeight,
                                 lowDistortionTypeComboBox, midDistortionTypeComboBox, highDistortionTypeComboBox);
    
    // Layout the band controls
    auto bandArea = getLocalBounds();
    bandArea.setTop(distortionBounds.getBottom() + 10);
    bandArea.setBottom(getLocalBounds().getBottom() - 20);

    auto bandWidth = bandArea.getWidth() / 3;
    auto lowBandArea = bandArea.removeFromLeft(bandWidth);
    auto midBandArea = bandArea.removeFromLeft(bandWidth);
    auto highBandArea = bandArea; // Remaining space for high band
    
    // Determine equal height for each slider within the band area
    int numSliders = 3; // Drive, Post-Gain, Mix
    int sliderHeight = lowBandArea.getHeight() / numSliders;

    // Assign sliders evenly within their respective areas
    lowBandDriveSlider.setBounds(lowBandArea.removeFromTop(sliderHeight));
    midBandDriveSlider.setBounds(midBandArea.removeFromTop(sliderHeight));
    highBandDriveSlider.setBounds(highBandArea.removeFromTop(sliderHeight));

    lowBandPostGainSlider.setBounds(lowBandArea.removeFromTop(sliderHeight));
    midBandPostGainSlider.setBounds(midBandArea.removeFromTop(sliderHeight));
    highBandPostGainSlider.setBounds(highBandArea.removeFromTop(sliderHeight));

    lowBandMixSlider.setBounds(lowBandArea);
    midBandMixSlider.setBounds(midBandArea);
    highBandMixSlider.setBounds(highBandArea);
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
        &crossoverLowSlider,
        &crossoverHighSlider,
        &lowBandDriveSlider,
        &midBandDriveSlider,
        &highBandDriveSlider,
        &lowBandPostGainSlider,
        &midBandPostGainSlider,
        &highBandPostGainSlider,
        &lowBandMixSlider,
        &midBandMixSlider,
        &highBandMixSlider,
        &lowDistortionTypeComboBox,
        &midDistortionTypeComboBox,
        &highDistortionTypeComboBox,
        &responseCurveComponent,
        &levelCompensationButton
    };
}
