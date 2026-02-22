/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "DSP/SpectrumAnalyzer.h"
#include "DSP/DynamicEQBand.h"

//==============================================================================
class DynamicEQAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    // numBands is the MAXIMUM number of bands (parameters are always registered for all)
    static constexpr int numBands = 8;

    // Active band count (runtime, 1..numBands)
    int  getActiveBandCount() const { return activeBandCount.load(); }
    void setActiveBandCount (int count);

    DynamicEQAudioProcessor();
    ~DynamicEQAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

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
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Public accessors for the editor
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    SpectrumAnalyzer& getPreSpectrumAnalyzer()  { return preSpectrum; }
    SpectrumAnalyzer& getPostSpectrumAnalyzer() { return postSpectrum; }

    float getBandGainReduction (int bandIndex) const;
    double getCurrentSampleRate() const { return lastSampleRate; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    // Active band count (default 4, max = numBands)
    std::atomic<int> activeBandCount { 4 };

    // DSP
    std::array<DynamicEQBand, numBands> bands;

    // Spectrum analysis
    SpectrumAnalyzer preSpectrum;
    SpectrumAnalyzer postSpectrum;

    double lastSampleRate = 44100.0;

    // Helper
    void updateBandParams (int bandIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicEQAudioProcessor)
};
