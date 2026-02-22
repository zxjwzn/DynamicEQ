/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Parameter ID helpers
//==============================================================================
// Kept for potential future use
// static juce::String getBandParamID (int bandIndex, const juce::String& paramName)
// {
//     return "band" + juce::String (bandIndex) + "_" + paramName;
// }

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
    DynamicEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Default frequencies for 8 bands
    const float defaultFreqs[numBands] = { 60.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 16000.0f };

    for (int i = 0; i < numBands; ++i)
    {
        auto prefix = "band" + juce::String (i) + "_";

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "freq", 1 },
            "Band " + juce::String (i + 1) + " Freq",
            juce::NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.25f),
            defaultFreqs[i]));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "gain", 1 },
            "Band " + juce::String (i + 1) + " Gain",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f),
            0.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "q", 1 },
            "Band " + juce::String (i + 1) + " Q",
            juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f),
            1.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "threshold", 1 },
            "Band " + juce::String (i + 1) + " Threshold",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f),
            -20.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "ratio", 1 },
            "Band " + juce::String (i + 1) + " Ratio",
            juce::NormalisableRange<float> (1.0f, 20.0f, 0.1f, 0.5f),
            4.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "attack", 1 },
            "Band " + juce::String (i + 1) + " Attack",
            juce::NormalisableRange<float> (0.1f, 200.0f, 0.1f, 0.4f),
            10.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "release", 1 },
            "Band " + juce::String (i + 1) + " Release",
            juce::NormalisableRange<float> (1.0f, 1000.0f, 1.0f, 0.4f),
            100.0f));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { prefix + "enabled", 1 },
            "Band " + juce::String (i + 1) + " Enabled",
            true));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { prefix + "dynamic", 1 },
            "Band " + juce::String (i + 1) + " Dynamic",
            true));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { prefix + "type", 1 },
            "Band " + juce::String (i + 1) + " Type",
            juce::StringArray { "Low Shelf", "Peak", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" },
            (i == 0) ? 0 : ((i == numBands - 1) ? 2 : 1)));
    }

    return layout;
}

//==============================================================================
DynamicEQAudioProcessor::DynamicEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       apvts (*this, nullptr, "Parameters", createParameterLayout())
#endif
{
}

DynamicEQAudioProcessor::~DynamicEQAudioProcessor()
{
}

//==============================================================================
const juce::String DynamicEQAudioProcessor::getName() const
{
    return "Dynamic EQ";
}

bool DynamicEQAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool DynamicEQAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool DynamicEQAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double DynamicEQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DynamicEQAudioProcessor::getNumPrograms()
{
    return 1;
}

int DynamicEQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DynamicEQAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String DynamicEQAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void DynamicEQAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void DynamicEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (getTotalNumOutputChannels());

    for (int i = 0; i < numBands; ++i)
    {
        bands[static_cast<size_t> (i)].prepare (spec);
        updateBandParams (i);
    }
}

void DynamicEQAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool DynamicEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void DynamicEQAudioProcessor::updateBandParams (int bandIndex)
{
    auto prefix = "band" + juce::String (bandIndex) + "_";

    BandParams p;
    p.frequency  = apvts.getRawParameterValue (prefix + "freq")->load();
    p.gain       = apvts.getRawParameterValue (prefix + "gain")->load();
    p.q          = apvts.getRawParameterValue (prefix + "q")->load();
    p.threshold  = apvts.getRawParameterValue (prefix + "threshold")->load();
    p.ratio      = apvts.getRawParameterValue (prefix + "ratio")->load();
    p.attackMs   = apvts.getRawParameterValue (prefix + "attack")->load();
    p.releaseMs  = apvts.getRawParameterValue (prefix + "release")->load();
    p.enabled    = apvts.getRawParameterValue (prefix + "enabled")->load() > 0.5f;
    p.dynamicOn  = apvts.getRawParameterValue (prefix + "dynamic")->load() > 0.5f;

    int typeIndex = static_cast<int> (apvts.getRawParameterValue (prefix + "type")->load());
    p.type = static_cast<BandParams::FilterType> (typeIndex);

    bands[static_cast<size_t> (bandIndex)].updateParams (p);
}

void DynamicEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Push pre-EQ spectrum data (mono sum)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        // Create mono mix for spectrum
        juce::AudioBuffer<float> monoBuffer (1, numSamples);
        monoBuffer.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            monoBuffer.addFrom (0, 0, buffer, ch, 0, numSamples, 1.0f / static_cast<float> (numChannels));

        preSpectrum.pushSamples (monoBuffer.getReadPointer (0), numSamples);
    }

    // Update and process each ACTIVE band only
    for (int i = 0; i < activeBandCount.load(); ++i)
    {
        updateBandParams (i);
        bands[static_cast<size_t> (i)].process (buffer);
    }

    // Push post-EQ spectrum data
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        juce::AudioBuffer<float> monoBuffer (1, numSamples);
        monoBuffer.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            monoBuffer.addFrom (0, 0, buffer, ch, 0, numSamples, 1.0f / static_cast<float> (numChannels));

        postSpectrum.pushSamples (monoBuffer.getReadPointer (0), numSamples);
    }
}

float DynamicEQAudioProcessor::getBandGainReduction (int bandIndex) const
{
    if (bandIndex >= 0 && bandIndex < numBands)
        return bands[static_cast<size_t> (bandIndex)].getGainReductionDB();
    return 0.0f;
}

void DynamicEQAudioProcessor::setActiveBandCount (int count)
{
    count = juce::jlimit (1, numBands, count);
    activeBandCount.store (count);
}

//==============================================================================
bool DynamicEQAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* DynamicEQAudioProcessor::createEditor()
{
    return new DynamicEQAudioProcessorEditor (*this);
}

//==============================================================================
void DynamicEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // Save active band count as a ValueTree property
    state.setProperty ("activeBandCount", activeBandCount.load(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DynamicEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        // Restore active band count
        if (tree.hasProperty ("activeBandCount"))
            activeBandCount.store (juce::jlimit (1, numBands, static_cast<int> (tree.getProperty ("activeBandCount"))));
        apvts.replaceState (tree);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DynamicEQAudioProcessor();
}
