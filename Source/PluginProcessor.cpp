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
    
    distortionProcessor.prepare(spec);
    
    leftChannelFifo.prepare(samplesPerBlock);
    rightChannelFifo.prepare(samplesPerBlock);
    
//    osc.initialise([](float x){return std::sin(x);});
//    spec.numChannels = getTotalNumOutputChannels();
//    osc.prepare(spec);
//    osc.setFrequency(2000);
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

void _3BandMultiEffectorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Avoid people getting screaming feedback
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    updateFilters();
    
    // Create an AudioBlock wrapper around the buffer for processing audio data in a flexible way
    juce::dsp::AudioBlock<float> block(buffer);
    
//    buffer.clear();
//    juce::dsp::ProcessContextReplacing<float> stereoContext(block);
//    osc.process(stereoContext);
    
    // Extract the single-channel blocks for the left and right audio channerls from the AudioBlock
    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = block.getSingleChannelBlock(1);
    
    // Create ProcessContextReplacing objects for both channels, representing the audio data to be processed
    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);
    
    leftChain.process(leftContext);
    rightChain.process(rightContext);
    
    auto preGainValue = apvts.getRawParameterValue("Pre Gain")->load();
    auto driveValue = apvts.getRawParameterValue("Drive")->load();
    auto postGainValue = apvts.getRawParameterValue("Post Gain")->load();
    
    buffer.applyGain(juce::Decibels::decibelsToGain(preGainValue));
    
    // Apply drive gain before waveshaper
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            if (driveValue > 0.f) {
                channelData[sample] *= driveValue;
            }q
        }
    }

    // Use a stateless function for waveshaper
    distortionProcessor.setWaveshaperFunction([](float x)
    {
        return std::tanh(x);
    });
    
    distortionProcessor.process(leftBlock);
    distortionProcessor.process(rightBlock);
    
    buffer.applyGain(juce::Decibels::decibelsToGain(postGainValue));
    
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
    
    auto* driveParameter = apvts.getRawParameterValue("Drive");
    jassert(driveParameter != nullptr); // Ensure the parameter exists
    if (driveParameter != nullptr)
        settings.distortionDrive = driveParameter->load();
    else
        settings.distortionDrive = 0.0f; // Default to a safe value
    
    settings.distortionDrive = apvts.getRawParameterValue("Drive")->load();
    
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

//============================================================================== Parameter Layout ==============================================================================//
juce::AudioProcessorValueTreeState::ParameterLayout _3BandMultiEffectorAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Low-Cut Frequency", 1),
                                                           "Low-Cut Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
                                                           20.f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("High-Cut Frequency", 1),
                                                           "High-Cut Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
                                                           20000.f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Peak Frequency", 1),
                                                           "Peak Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
                                                           750.f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Peak Gain", 1),
                                                           "Peak Gain",
                                                           juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f),
                                                           0.0f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Peak Quality", 1),
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
    
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("Low-Cut Slope", 1), "Low-Cut Slope", stringArray, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("High-Cut Slope", 1), "High-Cut Slope", stringArray, 0));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Drive", 1),
                                                           "Drive",
                                                           juce::NormalisableRange<float>(0.f, 50.f, 1.f, 1.f),
                                                           0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Pre Gain", 1),
                                                          "Pre Gain",
                                                          juce::NormalisableRange<float>(-40.f, 20.f, 1.f, 1.f),
                                                          0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Post Gain", 1),
                                                           "Post Gain",
                                                           juce::NormalisableRange<float>(-40.f, 20.f, 1.f, 1.f),
                                                           0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("Mix", 1),
                                                           "Mix",
                                                           juce::NormalisableRange<float>(0.f, 100.f, 1.f, 1.f),
                                                           50.f));
                                                           
    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new _3BandMultiEffectorAudioProcessor();
}
