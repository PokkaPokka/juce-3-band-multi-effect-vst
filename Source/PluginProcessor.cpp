/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

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
void _3BandMultiEffectorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Create a ProcessSpec object to hold processing specifications
    juce::dsp::ProcessSpec spec;
    
    // buffer size
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;
    spec.sampleRate = sampleRate;
    
    // Prepare both cahnnels processing chain using the specified config
    leftChain.prepare(spec);
    rightChain.prepare(spec);
    
    // Get current filter settings from the AudioProcessorValueTreeState (sliders)
    auto chainSettings = getChainSettings(apvts);

    updateFilters();
    
    // Prepare crossovers
    leftCrossover.prepare(spec);
    rightCrossover.prepare(spec);
    
    // Prepare distortion bands
    for (auto& band : leftBands) band.prepare(spec);
    for (auto& band : rightBands) band.prepare(spec);
    
    // Prepare temp buffers
    for (auto& buf : tempBuffers) buf.setSize(2, samplesPerBlock);
    
    leftChannelFifo.prepare(samplesPerBlock);
    rightChannelFifo.prepare(samplesPerBlock);
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

    // Avoid feedback by clearing unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
    
    // Update filters and parameters
    updateFilters();
    
    // Create an AudioBlock wrapper around the buffer for processing audio data in a flexible way
    juce::dsp::AudioBlock<float> block(buffer);
    
    // Extract the single-channel blocks for the left and right audio channerls from the AudioBlock
    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = block.getSingleChannelBlock(1);
    
    // Create ProcessContextReplacing objects for both channels, representing the audio data to be processed
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
    
    // Save EQ'd signal
    juce::AudioBuffer<float> eqBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    eqBuffer.makeCopyOf(buffer);
    buffer.clear();
    
    // Process each band
    processBand(eqBuffer, buffer, 0, leftCrossover.lowPassL, rightCrossover.lowPassL, leftBands[0], rightBands[0]);
    processBand(eqBuffer, buffer, 1, leftCrossover.highPassM, rightCrossover.highPassM, leftBands[1], rightBands[1]);
    processBand(eqBuffer, buffer, 2, leftCrossover.highPassH, rightCrossover.highPassH, leftBands[2], rightBands[2]);

    // Update FIFO buffers for visualization
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
//    return new _3BandMultiEffectorAudioProcessorEditor (*this);
    return new juce::GenericAudioProcessorEditor(*this);
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
    settings.lowBand.preGain = apvts.getRawParameterValue("LowBandPreGain")->load();
    settings.lowBand.postGain = apvts.getRawParameterValue("LowBandPostGain")->load();
    settings.lowBand.mix = apvts.getRawParameterValue("LowBandMix")->load();
    
    settings.midBand.type = static_cast<DistortionType>(apvts.getRawParameterValue("MidBandType")->load());
    settings.midBand.drive = apvts.getRawParameterValue("MidBandDrive")->load();
    settings.midBand.preGain = apvts.getRawParameterValue("MidBandPreGain")->load();
    settings.midBand.postGain = apvts.getRawParameterValue("MidBandPostGain")->load();
    settings.midBand.mix = apvts.getRawParameterValue("MidBandMix")->load();
    
    settings.highBand.type = static_cast<DistortionType>(apvts.getRawParameterValue("HighBandType")->load());
    settings.highBand.drive = apvts.getRawParameterValue("HighBandDrive")->load();
    settings.highBand.preGain = apvts.getRawParameterValue("HighBandPreGain")->load();
    settings.highBand.postGain = apvts.getRawParameterValue("HighBandPostGain")->load();
    settings.highBand.mix = apvts.getRawParameterValue("HighBandMix")->load();
    
    return settings;
}

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, chainSettings.peakFreq, chainSettings.peakQuality, juce::Decibels::decibelsToGain(chainSettings.peakGainInDeciibels));
}

void _3BandMultiEffectorAudioProcessor::updatePeakFilter(const ChainSettings &chainSettings)
{
    auto peakCoefficients = makePeakFilter(chainSettings, getSampleRate());
    updateCoefficients(leftChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
}

void updateCoefficients(Coefficients &old, const Coefficients &replacements)
{
    *old = *replacements;
}

void _3BandMultiEffectorAudioProcessor::updateLowCutFilters(const ChainSettings &chainSettings)
{
    auto lowCutCoefficient = makeLowCutFilter(chainSettings, getSampleRate());
    
    auto& leftLowCut = leftChain.get<ChainPositions::LowCut>();
    auto& rightLowCut = rightChain.get<ChainPositions::LowCut>();
    
    updateCutFilter(leftLowCut, lowCutCoefficient, chainSettings.lowCutSlope);
    updateCutFilter(rightLowCut, lowCutCoefficient, chainSettings.lowCutSlope);
}

void _3BandMultiEffectorAudioProcessor::updateHighCutFilters(const ChainSettings &chainSettings)
{
    auto highCutCoefficient = makeHighCutFilter(chainSettings, getSampleRate());

    auto& leftHighCut = leftChain.get<ChainPositions::HighCut>();
    auto& rightHighCut = rightChain.get<ChainPositions::HighCut>();

    updateCutFilter(leftHighCut, highCutCoefficient, chainSettings.highCutSlope);
    updateCutFilter(rightHighCut, highCutCoefficient, chainSettings.highCutSlope);
}

void _3BandMultiEffectorAudioProcessor::updateFilters()
{
    auto chainSettings = getChainSettings(apvts);
    updateLowCutFilters(chainSettings);
    updatePeakFilter(chainSettings);
    updateHighCutFilters(chainSettings);
}

//void _3BandMultiEffectorAudioProcessor::updateDistortion(juce::AudioBuffer<float>& buffer)
//{
//    // Get current filter settings from the AudioProcessorValueTreeState (sliders)
//    auto preGainValue = apvts.getRawParameterValue("Pre Gain")->load();
//    auto driveValue = apvts.getRawParameterValue("Drive")->load();
//    auto postGainValue = apvts.getRawParameterValue("Post Gain")->load();
//    auto mix = apvts.getRawParameterValue("Mix")->load() * 0.01f;
//    auto distortionType = int(apvts.getRawParameterValue("Distortion Type")->load());
//
//    // Set the mix ratio for the DryWetMixer
//    dryWetMixer.setWetMixProportion(mix);
//
//    // Push the dry buffer to the mixer before processing
//    dryWetMixer.pushDrySamples(buffer);
//    
//    // Create a copy of the input buffer for the wet signal
//    juce::AudioBuffer<float> wetBuffer(buffer);
//
//    // Apply pre-gain to the wet buffer
//    wetBuffer.applyGain(juce::Decibels::decibelsToGain(preGainValue));
//
//    // Apply drive gain before waveshaper
//    auto* channelDataLeft = wetBuffer.getWritePointer(0);
//    auto* channelDataRight = wetBuffer.getWritePointer(1);
//    for (int sample = 0; sample < wetBuffer.getNumSamples(); ++sample)
//    {
//        if (driveValue > 0.f) {
//            channelDataLeft[sample] *= driveValue;
//            channelDataRight[sample] *= driveValue;
//        }
//    }
//
//    // Apply waveshaping distortion
//    // Determine the distortion type and set the corresponding function
//    if (distortionType == 1) {
//        distortionProcessor.setWaveshaperFunction([](float x) { return std::tanh(x); });
//    } else if (distortionType == 2) {
//        distortionProcessor.setWaveshaperFunction([](float x) { return juce::jlimit(float(-0.1), float(0.1), x); });
//    } else {
//        // Default to tanh if unknown type
//        distortionProcessor.setWaveshaperFunction([](float x) { return x; });
//    }
//
//    juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
//    distortionProcessor.process(wetBlock);
//
//    // Apply post-gain to the wet buffer
//    wetBuffer.applyGain(juce::Decibels::decibelsToGain(postGainValue));
//
//    dryWetMixer.mixWetSamples(wetBuffer); // Mix in the wet (processed) signal
//
//    // Copy the mixed result back to the main buffer
//    buffer.copyFrom(0, 0, wetBuffer, 0, 0, buffer.getNumSamples());
//    buffer.copyFrom(1, 0, wetBuffer, 1, 0, buffer.getNumSamples());
//}

void CrossoverFilters::prepare(const juce::dsp::ProcessSpec& spec) {
    lowPassL.prepare(spec);
    highPassM.prepare(spec);
    lowPassM.prepare(spec);
    highPassH.prepare(spec);
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
    distortionProcessor.setPreGain(bandSettings.preGain);
    distortionProcessor.setDrive(bandSettings.drive);
    
    switch (bandSettings.type) {
        case DistortionType::SoftClipping:
            distortionProcessor.setWaveshaperFunction([](float x) { return std::tanh(x); });
            break;
        case DistortionType::HardClipping:
            distortionProcessor.setWaveshaperFunction([](float x) { return juce::jlimit(-1.0f, 1.0f, x); });
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

    // Get band-specific settings
    switch (bandIndex) {
        case 0: bandSettings = &settings.lowBand; break;
        case 1: bandSettings = &settings.midBand; break;
        case 2: bandSettings = &settings.highBand; break;
        default: jassertfalse; break;
    }

    if (!bandSettings) return;

    // Copy EQ'd signal to temp buffer
    tempBuffers[bandIndex].makeCopyOf(eqBuffer);

    // Process left channel
    auto leftBlock = juce::dsp::AudioBlock<float>(tempBuffers[bandIndex]).getSingleChannelBlock(0);
    {
        juce::dsp::ProcessContextReplacing<float> context(leftBlock);
        leftFilter.process(context);
        leftDistortion.process(context);
    }

    // Process right channel
    auto rightBlock = juce::dsp::AudioBlock<float>(tempBuffers[bandIndex]).getSingleChannelBlock(1);
    {
        juce::dsp::ProcessContextReplacing<float> context(rightBlock);
        rightFilter.process(context);
        rightDistortion.process(context);
    }

    // Calculate mix factors
    const float wetGain = bandSettings->mix * 0.01f;
    const float dryGain = 1.0f - wetGain;

    // Apply dry/wet mix
    for (int ch = 0; ch < output.getNumChannels(); ++ch) {
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
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("CrossoverLow", 107), "Crossover Low",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 200.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("CrossoverHigh", 108), "Crossover High",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 2000.0f));

    // Low Band Parameters
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("LowBandType", 109), "Low Band Type", distortionTypeArray, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("LowBandDrive", 110), "Low Band Drive",
        juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("LowBandPreGain", 111), "Low Band Pre Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("LowBandPostGain", 112), "Low Band Post Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("LowBandMix", 113), "Low Band Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f));
    
    // Mid Band Parameters
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("MidBandType", 114), "Mid Band Type", distortionTypeArray, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("MidBandDrive", 115), "Mid Band Drive",
        juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("MidBandPreGain", 116), "Mid Band Pre Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("MidBandPostGain", 117), "Mid Band Post Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("MidBandMix", 118), "Mid Band Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f));
    
    // High Band Parameters
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("HighBandType", 119), "High Band Type", distortionTypeArray, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("HighBandDrive", 120), "High Band Drive",
        juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("HighBandPreGain", 121), "High Band Pre Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("HighBandPostGain", 122), "High Band Post Gain",
        juce::NormalisableRange<float>(-40.0f, 20.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("HighBandMix", 123), "High Band Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f));

    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new _3BandMultiEffectorAudioProcessor();
}
