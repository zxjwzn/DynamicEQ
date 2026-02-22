/*
  ==============================================================================

    DynamicEQBand.h
    A single EQ band with parametric filter + dynamic (sidechain) compression

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
// Parameters for a single Dynamic EQ band
//==============================================================================
struct BandParams
{
    float frequency  = 1000.0f;   // Hz
    float gain       = 0.0f;      // dB (static gain)
    float q          = 1.0f;      // Q factor
    float threshold  = -20.0f;    // dB - dynamic threshold
    float ratio      = 4.0f;      // compression ratio
    float attackMs   = 10.0f;     // ms
    float releaseMs  = 100.0f;    // ms
    bool  enabled    = true;
    bool  dynamicOn  = true;      // enable dynamic behavior

    // Filter type
    enum class FilterType { LowShelf, Peak, HighShelf, LowCut, HighCut, Notch, BandPass };
    FilterType type = FilterType::Peak;
};

//==============================================================================
// Envelope follower for dynamic gain reduction
//==============================================================================
class EnvelopeFollower
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        envelope = 0.0f;
    }

    void setAttackRelease (float attackMs, float releaseMs)
    {
        if (sampleRate <= 0.0)
            return;
        attackCoeff  = std::exp (-1.0f / (static_cast<float> (sampleRate) * attackMs * 0.001f));
        releaseCoeff = std::exp (-1.0f / (static_cast<float> (sampleRate) * releaseMs * 0.001f));
    }

    float process (float inputLevel)
    {
        float coeff = (inputLevel > envelope) ? attackCoeff : releaseCoeff;
        envelope = coeff * envelope + (1.0f - coeff) * inputLevel;
        return envelope;
    }

    float getEnvelope() const { return envelope; }

private:
    double sampleRate = 44100.0;
    float attackCoeff  = 0.0f;
    float releaseCoeff = 0.0f;
    float envelope     = 0.0f;
};

//==============================================================================
// A single Dynamic EQ band processing unit
//==============================================================================
class DynamicEQBand
{
public:
    static constexpr int maxOrder = 2; // second-order IIR

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        envelopeFollower.prepare (sampleRate);

        for (auto& f : filters)
        {
            f.reset();
            f.prepare (spec);
        }

        // Sidechain bandpass filter for envelope detection
        sidechainFilter.reset();
        sidechainFilter.prepare (spec);

        gainReductionDB.store (0.0f);
    }

    void updateParams (const BandParams& p)
    {
        params = p;
        envelopeFollower.setAttackRelease (p.attackMs, p.releaseMs);
        updateFilterCoefficients (p.gain);
        updateSidechainFilter();
    }

    // Process audio in-place (stereo interleaved via AudioBuffer)
    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! params.enabled)
        {
            gainReductionDB.store (0.0f);
            return;
        }

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        if (! params.dynamicOn)
        {
            // Static EQ - just apply filter
            auto block = juce::dsp::AudioBlock<float> (buffer);
            auto context = juce::dsp::ProcessContextReplacing<float> (block);
            for (auto& f : filters)
                f.process (context);
            gainReductionDB.store (0.0f);
            return;
        }

        // Dynamic EQ processing: sample-by-sample gain computation
        // 1) Detect level using sidechain bandpass
        // 2) Compute gain reduction
        // 3) Apply dynamic gain via filter coefficient modulation

        // We'll do a simpler approach: compute per-block gain reduction
        // and blend the filter gain accordingly

        // Get sidechain level (mono sum)
        float peakLevel = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                peakLevel = std::max (peakLevel, std::abs (data[i]));
        }

        float levelDB = juce::Decibels::gainToDecibels (peakLevel, -100.0f);
        float envDB = juce::Decibels::gainToDecibels (
            envelopeFollower.process (juce::Decibels::decibelsToGain (levelDB, -100.0f)),
            -100.0f);

        // Compute gain reduction
        float reductionDB = 0.0f;
        if (envDB > params.threshold)
        {
            float excess = envDB - params.threshold;
            reductionDB = excess - excess / params.ratio;
        }

        gainReductionDB.store (reductionDB);

        // Apply dynamic gain: modulate the static gain by the reduction
        float dynamicGain = params.gain - reductionDB;
        updateFilterCoefficients (dynamicGain);

        auto block = juce::dsp::AudioBlock<float> (buffer);
        auto context = juce::dsp::ProcessContextReplacing<float> (block);
        for (auto& f : filters)
            f.process (context);
    }

    float getGainReductionDB() const { return gainReductionDB.load(); }
    const BandParams& getParams() const { return params; }

private:
    void updateFilterCoefficients (float gainDB)
    {
        if (sampleRate <= 0.0)
            return;

        juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>> coeffs;

        switch (params.type)
        {
            case BandParams::FilterType::LowShelf:
                coeffs.add (juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                    sampleRate, params.frequency, params.q, juce::Decibels::decibelsToGain (gainDB)));
                break;
            case BandParams::FilterType::Peak:
                coeffs.add (juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                    sampleRate, params.frequency, params.q, juce::Decibels::decibelsToGain (gainDB)));
                break;
            case BandParams::FilterType::HighShelf:
                coeffs.add (juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                    sampleRate, params.frequency, params.q, juce::Decibels::decibelsToGain (gainDB)));
                break;
            case BandParams::FilterType::LowCut:
                // High-pass filter (cuts low frequencies) — gain not applicable
                coeffs.add (juce::dsp::IIR::Coefficients<float>::makeHighPass (
                    sampleRate, params.frequency, params.q));
                break;
            case BandParams::FilterType::HighCut:
                // Low-pass filter (cuts high frequencies) — gain not applicable
                coeffs.add (juce::dsp::IIR::Coefficients<float>::makeLowPass (
                    sampleRate, params.frequency, params.q));
                break;
            case BandParams::FilterType::Notch:
            {
                // Standard biquad notch: b0=1, b1=-2cos(w0), b2=1, a0=1+alpha, a1=-2cos(w0), a2=1-alpha
                float w0    = juce::MathConstants<float>::twoPi * params.frequency / static_cast<float>(sampleRate);
                float cosW0 = std::cos(w0);
                float alpha = std::sin(w0) / (2.0f * params.q);
                coeffs.add (new juce::dsp::IIR::Coefficients<float> (
                    1.0f, -2.0f * cosW0, 1.0f,
                    1.0f + alpha, -2.0f * cosW0, 1.0f - alpha));
                break;
            }
            case BandParams::FilterType::BandPass:
                coeffs.add (juce::dsp::IIR::Coefficients<float>::makeBandPass (
                    sampleRate, params.frequency, params.q));
                break;
        }

        if (coeffs.size() > 0)
        {
            for (auto& f : filters)
                *f.state = *coeffs[0];
        }
    }

    void updateSidechainFilter()
    {
        if (sampleRate <= 0.0)
            return;

        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (
            sampleRate, params.frequency, params.q);
        *sidechainFilter.state = *coeffs;
    }

    BandParams params;
    double sampleRate = 44100.0;
    EnvelopeFollower envelopeFollower;

    // Stereo processing filter (duplicated for L/R via ProcessorDuplicator)
    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                   juce::dsp::IIR::Coefficients<float>>;
    std::array<Filter, 1> filters; // Single second-order section

    Filter sidechainFilter;

    std::atomic<float> gainReductionDB { 0.0f };
};
