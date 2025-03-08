/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/
#define _USE_MATH_DEFINES

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <math.h>

//==============================================================================
_3BandMultiEffectorAudioProcessor::_3BandMultiEffectorAudioProcessor()
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
}

_3BandMultiEffectorAudioProcessor::~_3BandMultiEffectorAudioProcessor()
{
}

//==============================================================================
const juce::String _3BandMultiEffectorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool _3BandMultiEffectorAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool _3BandMultiEffectorAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool _3BandMultiEffectorAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double _3BandMultiEffectorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int _3BandMultiEffectorAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int _3BandMultiEffectorAudioProcessor::getCurrentProgram()
{
    return 0;
}

void _3BandMultiEffectorAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String _3BandMultiEffectorAudioProcessor::getProgramName (int index)
{
    return {};
}

void _3BandMultiEffectorAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void _3BandMultiEffectorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1; // Mono processing for each chain
    spec.sampleRate = sampleRate;

    // Initialize oversampler
    oversampler.initProcessing(samplesPerBlock);
    auto oversampledSampleRate = sampleRate * oversampler.getOversamplingFactor();
    
    // Prepare spec for oversampled rate
    juce::dsp::ProcessSpec oversampledSpec = spec;
    oversampledSpec.sampleRate = oversampledSampleRate;
    oversampledSpec.maximumBlockSize = samplesPerBlock * oversampler.getOversamplingFactor();

    // Prepare chains with oversampled spec
    leftChain.prepare(oversampledSpec);
    rightChain.prepare(oversampledSpec);

    // Prepare crossovers
    leftCrossover.prepare(oversampledSpec);
    rightCrossover.prepare(oversampledSpec);

    // Prepare distortion bands
    for (auto& band : leftBands) band.prepare(oversampledSpec);
    for (auto& band : rightBands) band.prepare(oversampledSpec);

    // Prepare temp buffers for oversampled block size
    for (auto& buf : tempBuffers)
        buf.setSize(2, samplesPerBlock * oversampler.getOversamplingFactor());

    // Prepare FIFO buffers with original sample rate
    leftChannelFifo.prepare(samplesPerBlock);
    rightChannelFifo.prepare(samplesPerBlock);

    updateFilters();
}

void _3BandMultiEffectorAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool _3BandMultiEffectorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void _3BandMultiEffectorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Oversample the input buffer
    juce::dsp::AudioBlock<float> block(buffer);
    auto oversampledBlock = oversampler.processSamplesUp(block);

    // Update filters and parameters (at oversampled rate)
    updateFilters();

    // Process left and right channels at oversampled rate
    auto leftBlock = oversampledBlock.getSingleChannelBlock(0);
    auto rightBlock = oversampledBlock.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

    leftChain.process(leftContext);
    rightChain.process(rightContext);

    auto chainSettings = getChainSettings(apvts);

    // Update crossovers
    leftCrossover.update(chainSettings.crossoverLow, chainSettings.crossoverHigh);
    rightCrossover.update(chainSettings.crossoverLow, chainSettings.crossoverHigh);

    // Update band distortions
    updateBandDistortion(leftBands[0], chainSettings.lowBand);
    updateBandDistortion(leftBands[1], chainSettings.midBand);
    updateBandDistortion(leftBands[2], chainSettings.highBand);
    updateBandDistortion(rightBands[0], chainSettings.lowBand);
    updateBandDistortion(rightBands[1], chainSettings.midBand);
    updateBandDistortion(rightBands[2], chainSettings.highBand);

    // Convert oversampledBlock to AudioBuffer for EQ'd signal
    juce::AudioBuffer<float> eqBuffer(oversampledBlock.getNumChannels(), oversampledBlock.getNumSamples());
    eqBuffer.copyFrom(0, 0, oversampledBlock.getChannelPointer(0), oversampledBlock.getNumSamples());
    eqBuffer.copyFrom(1, 0, oversampledBlock.getChannelPointer(1), oversampledBlock.getNumSamples());

    // Create an output AudioBuffer for band processing
    juce::AudioBuffer<float> outputBuffer(oversampledBlock.getNumChannels(), oversampledBlock.getNumSamples());
    outputBuffer.clear();

    // Process each band at oversampled rate
    processBand(eqBuffer, outputBuffer, 0, leftCrossover.lowPassL, rightCrossover.lowPassL, leftBands[0], rightBands[0]);
    processBand(eqBuffer, outputBuffer, 1, leftCrossover.highPassM, rightCrossover.highPassM, leftBands[1], rightBands[1]);
    processBand(eqBuffer, outputBuffer, 2, leftCrossover.highPassH, rightCrossover.highPassH, leftBands[2], rightBands[2]);

    // Create an AudioBlock from the outputBuffer to match the oversampledBlock type
    juce::dsp::AudioBlock<float> outputBlock(outputBuffer);

    // Copy the processed output from outputBlock to oversampledBlock
    oversampledBlock.copyFrom(outputBlock);

    // Downsample back to original rate
    oversampler.processSamplesDown(block);

    // Update FIFO buffers with the downsampled buffer for visualization
    leftChannelFifo.update(buffer);
    rightChannelFifo.update(buffer);
}

//==============================================================================
bool _3BandMultiEffectorAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* _3BandMultiEffectorAudioProcessor::createEditor()
{
    return new _3BandMultiEffectorAudioProcessorEditor (*this);
//    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void _3BandMultiEffectorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    
    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);
}

void _3BandMultiEffectorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid()) {
        apvts.replaceState(tree);
        updateFilters();
    }
}

//============================================================================== Helper Functions ==============================================================================//
ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
    ChainSettings settings;
    
    settings.lowCutFreq = apvts.getRawParameterValue("Low-Cut Frequency")->load();
    settings.highCutFreq = apvts.getRawParameterValue("High-Cut Frequency")->load();
    settings.peakFreq = apvts.getRawParameterValue("Peak Frequency")->load();
    settings.peakGainInDeciibels = apvts.getRawParameterValue("Peak Gain")->load();
    settings.peakQuality = apvts.getRawParameterValue("Peak Quality")->load();
    settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("Low-Cut Slope")->load());
    settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("High-Cut Slope")->load());
    
    settings.crossoverLow = apvts.getRawParameterValue("CrossoverLow")->load();
    settings.crossoverHigh = apvts.getRawParameterValue("CrossoverHigh")->load();
    
    settings.lowBand.type = static_cast<DistortionType>(apvts.getRawParameterValue("LowBandType")->load());
    settings.lowBand.drive = apvts.getRawParameterValue("LowBandDrive")->load();
    settings.lowBand.postGain = apvts.getRawParameterValue("LowBandPostGain")->load();
    settings.lowBand.mix = apvts.getRawParameterValue("LowBandMix")->load();
    
    settings.midBand.type = static_cast<DistortionType>(apvts.getRawParameterValue("MidBandType")->load());
    settings.midBand.drive = apvts.getRawParameterValue("MidBandDrive")->load();
    settings.midBand.postGain = apvts.getRawParameterValue("MidBandPostGain")->load();
    settings.midBand.mix = apvts.getRawParameterValue("MidBandMix")->load();
    
    settings.highBand.type = static_cast<DistortionType>(apvts.getRawParameterValue("HighBandType")->load());
    settings.highBand.drive = apvts.getRawParameterValue("HighBandDrive")->load();
    settings.highBand.postGain = apvts.getRawParameterValue("HighBandPostGain")->load();
    settings.highBand.mix = apvts.getRawParameterValue("HighBandMix")->load();
    
    return settings;
}

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, chainSettings.peakFreq, chainSettings.peakQuality, juce::Decibels::decibelsToGain(chainSettings.peakGainInDeciibels));
}

void _3BandMultiEffectorAudioProcessor::updatePeakFilter(const ChainSettings &chainSettings, float sampleRate)
{
    auto peakCoefficients = makePeakFilter(chainSettings, sampleRate);
    updateCoefficients(leftChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
}

void updateCoefficients(Coefficients &old, const Coefficients &replacements)
{
    *old = *replacements;
}

void _3BandMultiEffectorAudioProcessor::updateLowCutFilters(const ChainSettings &chainSettings, float sampleRate)
{
    auto lowCutCoefficient = makeLowCutFilter(chainSettings, sampleRate);
    
    auto& leftLowCut = leftChain.get<ChainPositions::LowCut>();
    auto& rightLowCut = rightChain.get<ChainPositions::LowCut>();
    
    updateCutFilter(leftLowCut, lowCutCoefficient, chainSettings.lowCutSlope);
    updateCutFilter(rightLowCut, lowCutCoefficient, chainSettings.lowCutSlope);
}

void _3BandMultiEffectorAudioProcessor::updateHighCutFilters(const ChainSettings &chainSettings, float sampleRate)
{
    auto highCutCoefficient = makeHighCutFilter(chainSettings, sampleRate);

    auto& leftHighCut = leftChain.get<ChainPositions::HighCut>();
    auto& rightHighCut = rightChain.get<ChainPositions::HighCut>();

    updateCutFilter(leftHighCut, highCutCoefficient, chainSettings.highCutSlope);
    updateCutFilter(rightHighCut, highCutCoefficient, chainSettings.highCutSlope);
}

void _3BandMultiEffectorAudioProcessor::updateFilters()
{
    auto chainSettings = getChainSettings(apvts);
    auto oversampledSampleRate = getSampleRate() * oversampler.getOversamplingFactor();
    updateLowCutFilters(chainSettings, oversampledSampleRate);
    updatePeakFilter(chainSettings, oversampledSampleRate);
    updateHighCutFilters(chainSettings, oversampledSampleRate);
}

void CrossoverFilters::prepare(const juce::dsp::ProcessSpec& spec)
{
    lowPassL.prepare(spec);
    highPassM.prepare(spec);
    lowPassM.prepare(spec);
    highPassH.prepare(spec);

    // Set each filter's type:
    lowPassL.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    highPassM.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    lowPassM.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    highPassH.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
}

void CrossoverFilters::update(float crossoverLow, float crossoverHigh) {
    lowPassL.setCutoffFrequency(crossoverLow);
    highPassM.setCutoffFrequency(crossoverLow);
    lowPassM.setCutoffFrequency(crossoverHigh);
    highPassH.setCutoffFrequency(crossoverHigh);
}

void _3BandMultiEffectorAudioProcessor::updateBandDistortion(
    Distortion<float>& distortionProcessor,
    const BandSettings& bandSettings)
{
    // Set parameters for this band's distortion
    float drive = bandSettings.drive;
    distortionProcessor.setDrive(drive);
    
    switch (bandSettings.type) {
        case DistortionType::SoftClipping:
            distortionProcessor.setWaveshaperFunction([](float x) { return std::tanh(x); });
            break;
        case DistortionType::HardClipping:
            distortionProcessor.setWaveshaperFunction([](float x) { return juce::jlimit (float (-0.1), float (0.1), x); });
            break;
        case DistortionType::ArcTan:
            distortionProcessor.setWaveshaperFunction([](float x) -> float { return 2 / M_PI * std::atan(x); });
            break;
        case DistortionType::BitCrusher:
            distortionProcessor.reduceBitDepth(bandSettings.drive);
            break;
        default:
            distortionProcessor.setWaveshaperFunction([](float x) { return std::tanh(x); });
            break;
    }
    
    distortionProcessor.setPostGain(bandSettings.postGain);
}

void _3BandMultiEffectorAudioProcessor::processBand(
    const juce::AudioBuffer<float>& eqBuffer,
    juce::AudioBuffer<float>& output,
    int bandIndex,
    juce::dsp::LinkwitzRileyFilter<float>& leftFilter,
    juce::dsp::LinkwitzRileyFilter<float>& rightFilter,
    Distortion<float>& leftDistortion,
    Distortion<float>& rightDistortion)
{
    auto settings = getChainSettings(apvts);
    BandSettings* bandSettings = nullptr;
    switch (bandIndex)
    {
        case 0: bandSettings = &settings.lowBand; break;
        case 1: bandSettings = &settings.midBand; break;
        case 2: bandSettings = &settings.highBand; break;
        default: jassertfalse; break;
    }
    if (!bandSettings)
        return;

    tempBuffers[bandIndex].makeCopyOf(eqBuffer);

    if (bandIndex == 1) // Mid band: chain both high-pass and low-pass filters
    {
        // Process left channel
        auto leftBlock = juce::dsp::AudioBlock<float>(tempBuffers[bandIndex]).getSingleChannelBlock(0);
        {
            juce::dsp::ProcessContextReplacing<float> context(leftBlock);
            leftCrossover.highPassM.process(context); // High-pass (cutoff at crossoverLow)
            leftCrossover.lowPassM.process(context);  // Low-pass (cutoff at crossoverHigh)
            if (bandSettings->drive > 0.0f)
                leftDistortion.process(context);
        }

        // Process right channel
        auto rightBlock = juce::dsp::AudioBlock<float>(tempBuffers[bandIndex]).getSingleChannelBlock(1);
        {
            juce::dsp::ProcessContextReplacing<float> context(rightBlock);
            rightCrossover.highPassM.process(context);
            rightCrossover.lowPassM.process(context);

            if (bandSettings->drive > 0.0f)
                rightDistortion.process(context);
        }
    }
    else // Low and high bands
    {
        // Process left channel
        auto leftBlock = juce::dsp::AudioBlock<float>(tempBuffers[bandIndex]).getSingleChannelBlock(0);
        {
            juce::dsp::ProcessContextReplacing<float> context(leftBlock);
            leftFilter.process(context);
            if (bandSettings->drive > 0.0f)
                leftDistortion.process(context);
        }

        // Process right channel
        auto rightBlock = juce::dsp::AudioBlock<float>(tempBuffers[bandIndex]).getSingleChannelBlock(1);
        {
            juce::dsp::ProcessContextReplacing<float> context(rightBlock);
            rightFilter.process(context);
            if (bandSettings->drive > 0.0f)
                rightDistortion.process(context);
        }
    }

    // Apply dry/wet mix
    const float wetGain = bandSettings->mix * 0.01f;
    const float dryGain = 1.0f - wetGain;

    for (int ch = 0; ch < output.getNumChannels(); ++ch)
    {
        output.addFrom(ch, 0,
                       eqBuffer, ch, 0, output.getNumSamples(), // Dry signal
                       dryGain);

        output.addFrom(ch, 0,
                       tempBuffers[bandIndex], ch, 0, output.getNumSamples(), // Wet signal
                       wetGain);
    }
}

//============================================================================== Parameter Layout ==============================================================================//
juce::AudioProcessorValueTreeState::ParameterLayout _3BandMultiEffectorAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Low-Cut Frequency", 100),
                                                           "Low-Cut Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
                                                           20.f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("High-Cut Frequency", 101),
                                                           "High-Cut Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
                                                           20000.f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Peak Frequency", 102),
                                                           "Peak Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
                                                           750.f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Peak Gain", 103),
                                                           "Peak Gain",
                                                           juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f),
                                                           0.0f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Peak Quality", 104),
                                                           "Peak Quality",
                                                           juce::NormalisableRange<float>(0.1f, 50.f, 0.05f, 1.f),
                                                           1.f));
    
    juce::StringArray stringArray;
    juce::String unit = " dB/Oct";
    for (int i = 0; i < 4; ++i) {
        juce::String str;
        str << (12 + i * 12);
        stringArray.add(str + unit);
    }
    
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("Low-Cut Slope", 105), "Low-Cut Slope", stringArray, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("High-Cut Slope", 106), "High-Cut Slope", stringArray, 0));
    
    juce::StringArray distortionTypeArray;
    distortionTypeArray.add("Soft Clipping");
    distortionTypeArray.add("Hard Clipping");
    distortionTypeArray.add("ArcTan Distortion");
    distortionTypeArray.add("Bit Crushing");
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("CrossoverLow", 107), "Crossover Low",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 200.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("CrossoverHigh", 108), "Crossover High",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 2000.0f));

    // Low Band Parameters
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("LowBandType", 109), "Low Band Type", distortionTypeArray, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("LowBandDrive", 110), "Low Band Drive",
        juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("LowBandPostGain", 112), "Low Band Post Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("LowBandMix", 113), "Low Band Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));
    
    // Mid Band Parameters
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("MidBandType", 114), "Mid Band Type", distortionTypeArray, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("MidBandDrive", 115), "Mid Band Drive",
        juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("MidBandPostGain", 117), "Mid Band Post Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("MidBandMix", 118), "Mid Band Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));
    
    // High Band Parameters
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("HighBandType", 119), "High Band Type", distortionTypeArray, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("HighBandDrive", 120), "High Band Drive",
        juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("HighBandPostGain", 122), "High Band Post Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("HighBandMix", 123), "High Band Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));

    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new _3BandMultiEffectorAudioProcessor();
}
