/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

template<typename T>
struct Fifo
{
    void prepare(int numChannels, int numSamples)
    {
        static_assert( std::is_same_v<T, juce::AudioBuffer<float>>,
                      "prepare(numChannels, numSamples) should only be used when the Fifo is holding juce::AudioBuffer<float>");
        for( auto& buffer : buffers)
        {
            buffer.setSize(numChannels,
                           numSamples,
                           false,   //clear everything?
                           true,    //including the extra space?
                           true);   //avoid reallocating if you can?
            buffer.clear();
        }
    }
    
    void prepare(size_t numElements)
    {
        static_assert( std::is_same_v<T, std::vector<float>>,
                      "prepare(numElements) should only be used when the Fifo is holding std::vector<float>");
        for( auto& buffer : buffers )
        {
            buffer.clear();
            buffer.resize(numElements, 0);
        }
    }
    
    bool push(const T& t)
    {
        auto write = fifo.write(1);
        if( write.blockSize1 > 0 )
        {
            buffers[write.startIndex1] = t;
            return true;
        }
        
        return false;
    }
    
    bool pull(T& t)
    {
        auto read = fifo.read(1);
        if( read.blockSize1 > 0 )
        {
            t = buffers[read.startIndex1];
            return true;
        }
        
        return false;
    }
    
    int getNumAvailableForReading() const
    {
        return fifo.getNumReady();
    }
private:
    static constexpr int Capacity = 30;
    std::array<T, Capacity> buffers;
    juce::AbstractFifo fifo {Capacity};
};

enum Channel
{
    Right, //effectively 0
    Left //effectively 1
};

template<typename BlockType>
struct SingleChannelSampleFifo
{
    SingleChannelSampleFifo(Channel ch) : channelToUse(ch)
    {
        prepared.set(false);
    }
    
    void update(const BlockType& buffer)
    {
        jassert(prepared.get());
        jassert(buffer.getNumChannels() > channelToUse );
        auto* channelPtr = buffer.getReadPointer(channelToUse);
        
        for( int i = 0; i < buffer.getNumSamples(); ++i )
        {
            pushNextSampleIntoFifo(channelPtr[i]);
        }
    }

    void prepare(int bufferSize)
    {
        prepared.set(false);
        size.set(bufferSize);
        
        bufferToFill.setSize(1,             //channel
                             bufferSize,    //num samples
                             false,         //keepExistingContent
                             true,          //clear extra space
                             true);         //avoid reallocating
        audioBufferFifo.prepare(1, bufferSize);
        fifoIndex = 0;
        prepared.set(true);
    }
    //==============================================================================
    int getNumCompleteBuffersAvailable() const { return audioBufferFifo.getNumAvailableForReading(); }
    bool isPrepared() const { return prepared.get(); }
    int getSize() const { return size.get(); }
    //==============================================================================
    bool getAudioBuffer(BlockType& buf) { return audioBufferFifo.pull(buf); }
private:
    Channel channelToUse;
    int fifoIndex = 0;
    Fifo<BlockType> audioBufferFifo;
    BlockType bufferToFill;
    juce::Atomic<bool> prepared = false;
    juce::Atomic<int> size = 0;
    
    void pushNextSampleIntoFifo(float sample)
    {
        if (fifoIndex == bufferToFill.getNumSamples())
        {
            auto ok = audioBufferFifo.push(bufferToFill);

            juce::ignoreUnused(ok);
            
            fifoIndex = 0;
        }
        
        bufferToFill.setSample(0, fifoIndex, sample);
        ++fifoIndex;
    }
};

enum Slope
{
    Slope_12,
    Slope_24,
    Slope_36,
    Slope_48
};

enum DistortionType
{
    SoftClipping,
    HardClipping
};

struct BandSettings {
    DistortionType type{ DistortionType::SoftClipping };
    float drive{ 0.0f }, postGain{ 0.0f }, mix{ 100.0f };
};

struct ChainSettings
{
    float peakFreq {0}, peakGainInDeciibels{0}, peakQuality{1.f};
    float lowCutFreq{0}, highCutFreq{0};
    Slope lowCutSlope {Slope::Slope_12}, highCutSlope {Slope::Slope_12};
    DistortionType distortionType {DistortionType::SoftClipping};
    float crossoverLow{ 200.0f }, crossoverHigh{ 2000.0f };
    BandSettings lowBand, midBand, highBand;
};

// A template class for distortion effects
template <typename FloatType>
class Distortion
{
public:
    Distortion()
    {
        processorChain.template get<waveshaperIndex>().functionToUse = [](FloatType x) {
            return std::tanh(x);
        };
        processorChain.template get<postGainIndex>().setGainDecibels(0.0f);
    }

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        processorChain.prepare(spec);
    }

    void setPostGain(FloatType gain)
    {
        processorChain.template get<postGainIndex>().setGainDecibels(gain);
    }

    
    void setWaveshaperFunction(float (*func)(float))
    {
        processorChain.template get<waveshaperIndex>().functionToUse = func;
    }

    void process(juce::dsp::ProcessContextReplacing<float>& context)
    {
        processorChain.process(context);
    }
    
    void setDrive(float driveLinear) {
        processorChain.template get<driveIndex>().setGainLinear(driveLinear);
    }

private:
    enum
    {
        driveIndex,
        waveshaperIndex,
        postGainIndex
    };

    juce::dsp::ProcessorChain<
        juce::dsp::Gain<float>,   // Drive (linear)
        juce::dsp::WaveShaper<float>,
        juce::dsp::Gain<float>    // Post-gain (dB)
    > processorChain;
};

struct CrossoverFilters {
    juce::dsp::LinkwitzRileyFilter<float> lowPassL, highPassM, lowPassM, highPassH;
    void prepare(const juce::dsp::ProcessSpec& spec);
    void update(float crossoverLow, float crossoverHigh);
    std::array<float, 2> getCutoffFrequencies() const {
        return { lowPassL.getCutoffFrequency(), highPassH.getCutoffFrequency() };
    }
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

inline auto makeLowCutFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(chainSettings.lowCutFreq, sampleRate, 2 * (chainSettings.lowCutSlope + 1));
}

inline auto makeHighCutFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(chainSettings.highCutFreq, sampleRate, 2 * (chainSettings.highCutSlope + 1));
}
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

    using BlockType = juce::AudioBuffer<float>;
    SingleChannelSampleFifo<BlockType> leftChannelFifo{Channel::Left};
    SingleChannelSampleFifo<BlockType> rightChannelFifo{Channel::Right};
    
    Distortion<float> distortionProcessor;
    
private:
    MonoChain leftChain, rightChain;
    
    juce::dsp::Oversampling<float> oversampler{2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR};
    
    // Update the peak filter coefficients (frequency, gain, and quality factor)
    // based on user settings stored in ChainSettings
    void updatePeakFilter(const ChainSettings& chainSettings, float sampleRate);
    
    void updateLowCutFilters(const ChainSettings& chainSettings, float sampleRate);
    void updateHighCutFilters(const ChainSettings& chainSettings, float sampleRate);
    void updateFilters();
    void updateBandDistortion(Distortion<float>& distortionProcessor, const BandSettings& bandSettings);
    void processBand(
        const juce::AudioBuffer<float>& eqBuffer,
        juce::AudioBuffer<float>& output,
        int bandIndex,
        juce::dsp::LinkwitzRileyFilter<float>& leftFilter,
        juce::dsp::LinkwitzRileyFilter<float>& rightFilter,
        Distortion<float>& leftDistortion,
        Distortion<float>& rightDistortion);
    juce::dsp::Oscillator<float> osc;
    juce::dsp::DryWetMixer<float> dryWetMixer;
    CrossoverFilters leftCrossover, rightCrossover;
    Distortion<float> leftBands[3], rightBands[3]; // 0: low, 1: mid, 2: high
    juce::AudioBuffer<float> tempBuffers[3]; // For band processing
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (_3BandMultiEffectorAudioProcessor)
};
