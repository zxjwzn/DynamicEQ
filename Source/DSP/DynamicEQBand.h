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

    // Channel processing mode for stereo separation
    enum class ChannelMode { Stereo, Left, Right, Mid, Side };
    ChannelMode channelMode = ChannelMode::Stereo;
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
    static constexpr int maxChannels = 2; // L and R (or M and S)

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        envelopeFollower.prepare (sampleRate);

        // Prepare per-channel IIR filters (always 2 channels for L/R or M/S)
        juce::dsp::ProcessSpec monoSpec;
        monoSpec.sampleRate       = spec.sampleRate;
        monoSpec.maximumBlockSize = spec.maximumBlockSize;
        monoSpec.numChannels      = 1;

        for (auto& f : channelFilters)
        {
            f.reset();
            f.prepare (monoSpec);
        }

        gainReductionDB.store (0.0f);
    }

    void updateParams (const BandParams& p)
    {
        params = p;
        envelopeFollower.setAttackRelease (p.attackMs, p.releaseMs);
        updateFilterCoefficients (p.gain);
    }

    // Process audio in-place (stereo AudioBuffer)
    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! params.enabled)
        {
            gainReductionDB.store (0.0f);
            return;
        }

        const int numChannels = buffer.getNumChannels();
        const bool isStereoBuffer = (numChannels >= 2);
        const auto mode = params.channelMode;

        // --- M/S encode if needed ---
        if (isStereoBuffer && (mode == BandParams::ChannelMode::Mid || mode == BandParams::ChannelMode::Side))
            encodeMidSide (buffer);

        if (! params.dynamicOn)
        {
            // Static EQ processing
            applyFilters (buffer, mode, isStereoBuffer);
            gainReductionDB.store (0.0f);
        }
        else
        {
            // Dynamic EQ: detect level on target channel(s), compute reduction, apply
            float peakLevel = detectLevel (buffer, mode, isStereoBuffer);

            float levelDB = juce::Decibels::gainToDecibels (peakLevel, -100.0f);
            float envDB = juce::Decibels::gainToDecibels (
                envelopeFollower.process (juce::Decibels::decibelsToGain (levelDB, -100.0f)),
                -100.0f);

            float reductionDB = 0.0f;
            if (envDB > params.threshold)
            {
                float excess = envDB - params.threshold;
                reductionDB = excess - excess / params.ratio;
            }

            gainReductionDB.store (reductionDB);

            float dynamicGain = params.gain - reductionDB;
            updateFilterCoefficients (dynamicGain);

            applyFilters (buffer, mode, isStereoBuffer);
        }

        // --- M/S decode if needed ---
        if (isStereoBuffer && (mode == BandParams::ChannelMode::Mid || mode == BandParams::ChannelMode::Side))
            decodeMidSide (buffer);
    }

    float getGainReductionDB() const { return gainReductionDB.load(); }
    const BandParams& getParams() const { return params; }

private:
    //==============================================================================
    // Mid/Side encoding: L,R -> M,S  where M=(L+R)*0.5, S=(L-R)*0.5
    //==============================================================================
    static void encodeMidSide (juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        float* left  = buffer.getWritePointer (0);
        float* right = buffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            float l = left[i];
            float r = right[i];
            left[i]  = (l + r) * 0.5f;  // Mid
            right[i] = (l - r) * 0.5f;  // Side
        }
    }

    //==============================================================================
    // Mid/Side decoding: M,S -> L,R  where L=M+S, R=M-S
    //==============================================================================
    static void decodeMidSide (juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        float* mid  = buffer.getWritePointer (0);
        float* side = buffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            float m = mid[i];
            float s = side[i];
            mid[i]  = m + s;  // Left
            side[i] = m - s;  // Right
        }
    }

    //==============================================================================
    // Detect peak level on the target channel(s) for envelope following
    //==============================================================================
    float detectLevel (const juce::AudioBuffer<float>& buffer,
                       BandParams::ChannelMode mode, bool isStereo) const
    {
        const int numSamples = buffer.getNumSamples();
        float peakLevel = 0.0f;

        // Determine which channel(s) to detect on
        // After M/S encode: ch0=Mid, ch1=Side
        // For L/R mode: ch0=Left, ch1=Right
        switch (mode)
        {
            case BandParams::ChannelMode::Left:
            case BandParams::ChannelMode::Mid:
            {
                const float* data = buffer.getReadPointer (0);
                for (int i = 0; i < numSamples; ++i)
                    peakLevel = std::max (peakLevel, std::abs (data[i]));
                break;
            }
            case BandParams::ChannelMode::Right:
            case BandParams::ChannelMode::Side:
            {
                if (isStereo)
                {
                    const float* data = buffer.getReadPointer (1);
                    for (int i = 0; i < numSamples; ++i)
                        peakLevel = std::max (peakLevel, std::abs (data[i]));
                }
                break;
            }
            case BandParams::ChannelMode::Stereo:
            default:
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                {
                    const float* data = buffer.getReadPointer (ch);
                    for (int i = 0; i < numSamples; ++i)
                        peakLevel = std::max (peakLevel, std::abs (data[i]));
                }
                break;
            }
        }

        return peakLevel;
    }

    //==============================================================================
    // Apply IIR filters to the appropriate channel(s) based on channel mode
    //==============================================================================
    void applyFilters (juce::AudioBuffer<float>& buffer,
                       BandParams::ChannelMode mode, bool isStereo)
    {
        const int numSamples = buffer.getNumSamples();

        switch (mode)
        {
            case BandParams::ChannelMode::Stereo:
            {
                // Process both channels
                processChannel (buffer, 0, numSamples, channelFilters[0]);
                if (isStereo)
                    processChannel (buffer, 1, numSamples, channelFilters[1]);
                break;
            }
            case BandParams::ChannelMode::Left:
            case BandParams::ChannelMode::Mid:   // After M/S encode, ch0 = Mid
            {
                processChannel (buffer, 0, numSamples, channelFilters[0]);
                break;
            }
            case BandParams::ChannelMode::Right:
            case BandParams::ChannelMode::Side:  // After M/S encode, ch1 = Side
            {
                if (isStereo)
                    processChannel (buffer, 1, numSamples, channelFilters[1]);
                break;
            }
        }
    }

    //==============================================================================
    // Process a single channel through its IIR filter
    //==============================================================================
    static void processChannel (juce::AudioBuffer<float>& buffer, int channel, int numSamples,
                                juce::dsp::IIR::Filter<float>& filter)
    {
        float* data = buffer.getWritePointer (channel);
        for (int i = 0; i < numSamples; ++i)
            data[i] = filter.processSample (data[i]);
    }

    //==============================================================================
    void updateFilterCoefficients (float gainDB)
    {
        if (sampleRate <= 0.0)
            return;

        juce::dsp::IIR::Coefficients<float>::Ptr coeffs;

        switch (params.type)
        {
            case BandParams::FilterType::LowShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                    sampleRate, params.frequency, params.q, juce::Decibels::decibelsToGain (gainDB));
                break;
            case BandParams::FilterType::Peak:
                coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                    sampleRate, params.frequency, params.q, juce::Decibels::decibelsToGain (gainDB));
                break;
            case BandParams::FilterType::HighShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                    sampleRate, params.frequency, params.q, juce::Decibels::decibelsToGain (gainDB));
                break;
            case BandParams::FilterType::LowCut:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (
                    sampleRate, params.frequency, params.q);
                break;
            case BandParams::FilterType::HighCut:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (
                    sampleRate, params.frequency, params.q);
                break;
            case BandParams::FilterType::Notch:
            {
                float w0    = juce::MathConstants<float>::twoPi * params.frequency / static_cast<float>(sampleRate);
                float cosW0 = std::cos(w0);
                float alpha = std::sin(w0) / (2.0f * params.q);
                coeffs = new juce::dsp::IIR::Coefficients<float> (
                    1.0f, -2.0f * cosW0, 1.0f,
                    1.0f + alpha, -2.0f * cosW0, 1.0f - alpha);
                break;
            }
            case BandParams::FilterType::BandPass:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (
                    sampleRate, params.frequency, params.q);
                break;
        }

        if (coeffs != nullptr)
        {
            for (auto& f : channelFilters)
                f.coefficients = coeffs;
        }
    }

    BandParams params;
    double sampleRate = 44100.0;
    EnvelopeFollower envelopeFollower;

    // Per-channel IIR filters: [0]=Left/Mid, [1]=Right/Side
    std::array<juce::dsp::IIR::Filter<float>, maxChannels> channelFilters;

    std::atomic<float> gainReductionDB { 0.0f };
};
