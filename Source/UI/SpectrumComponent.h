/*
  ==============================================================================

    SpectrumComponent.h
    Combined spectrum analyzer + EQ curve + interactive draggable nodes

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

//==============================================================================
// Helper: map frequency (Hz) to x position in a given width (log scale)
//==============================================================================
inline float freqToX(float freq, float width, float minFreq = 20.0f, float maxFreq = 20000.0f)
{
    return width * (std::log(freq / minFreq) / std::log(maxFreq / minFreq));
}

inline float xToFreq(float x, float width, float minFreq = 20.0f, float maxFreq = 20000.0f)
{
    return minFreq * std::pow(maxFreq / minFreq, x / width);
}

// Map dB to y position within component height
inline float dbToY(float db, float height, float minDB = -24.0f, float maxDB = 24.0f)
{
    return juce::jmap(db, maxDB, minDB, 0.0f, height);
}

inline float yToDb(float y, float height, float minDB = -24.0f, float maxDB = 24.0f)
{
    return juce::jmap(y, 0.0f, height, maxDB, minDB);
}

//==============================================================================
// Band colors
//==============================================================================
inline juce::Colour getBandColour(int bandIndex)
{
    const juce::Colour colours[] = {
        juce::Colour(0xFFFF6B6B), // Red-ish
        juce::Colour(0xFFFFD93D), // Yellow
        juce::Colour(0xFF6BCB77), // Green
        juce::Colour(0xFF4D96FF), // Blue
    };
    return colours[bandIndex % 4];
}

//==============================================================================
class SpectrumComponent : public juce::Component,
                          public juce::Timer
{
public:
    // Spectrum display modes
    enum class SpectrumDisplayMode { LR_Mix, LR_Split, MS_Split };

    SpectrumComponent(DynamicEQAudioProcessor &p)
        : processor(p)
    {
        setOpaque(true);
        startTimerHz(60); // 60 fps refresh

        smoothedPreSpectrum.fill(0.0f);
        smoothedPostSpectrum.fill(0.0f);
        smoothedPreL.fill(0.0f);
        smoothedPreR.fill(0.0f);
        smoothedPostL.fill(0.0f);
        smoothedPostR.fill(0.0f);

        // Spectrum mode button
        spectrumModeBtn.setButtonText ("L+R");
        spectrumModeBtn.setTooltip (juce::String::fromUTF8 ("\u5207\u6362\u9891\u8c31\u663e\u793a\u6a21\u5f0f"));
        spectrumModeBtn.onClick = [this]()
        {
            switch (spectrumDisplayMode)
            {
                case SpectrumDisplayMode::LR_Mix:
                    spectrumDisplayMode = SpectrumDisplayMode::LR_Split;
                    spectrumModeBtn.setButtonText ("L/R");
                    break;
                case SpectrumDisplayMode::LR_Split:
                    spectrumDisplayMode = SpectrumDisplayMode::MS_Split;
                    spectrumModeBtn.setButtonText ("M/S");
                    break;
                case SpectrumDisplayMode::MS_Split:
                    spectrumDisplayMode = SpectrumDisplayMode::LR_Mix;
                    spectrumModeBtn.setButtonText ("L+R");
                    break;
            }
        };
        addAndMakeVisible(spectrumModeBtn);
    }

    ~SpectrumComponent() override
    {
        stopTimer();
    }

    //==============================================================================
    void paint(juce::Graphics &g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Dark background
        g.fillAll(juce::Colour(0xFF1A1A2E));

        // Draw grid
        drawGrid(g, bounds);

        // Draw spectrum based on display mode
        switch (spectrumDisplayMode)
        {
            case SpectrumDisplayMode::LR_Mix:
            {
                // Pre-EQ spectrum (dimmer)
                drawSpectrum(g, bounds, smoothedPreSpectrum, juce::Colour(0x30FFFFFF), juce::Colour(0x08FFFFFF));
                // Post-EQ spectrum (brighter)
                drawSpectrum(g, bounds, smoothedPostSpectrum, juce::Colour(0x6000D4FF), juce::Colour(0x1800D4FF));
                break;
            }
            case SpectrumDisplayMode::LR_Split:
            {
                // Pre-EQ: L=blue dim, R=red dim
                drawSpectrum(g, bounds, smoothedPreL, juce::Colour(0x204D96FF), juce::Colour(0x064D96FF));
                drawSpectrum(g, bounds, smoothedPreR, juce::Colour(0x20FF6B6B), juce::Colour(0x06FF6B6B));
                // Post-EQ: L=blue, R=red
                drawSpectrum(g, bounds, smoothedPostL, juce::Colour(0x604D96FF), juce::Colour(0x184D96FF));
                drawSpectrum(g, bounds, smoothedPostR, juce::Colour(0x60FF6B6B), juce::Colour(0x18FF6B6B));
                break;
            }
            case SpectrumDisplayMode::MS_Split:
            {
                // Pre-EQ: M=white dim, S=yellow dim
                drawSpectrum(g, bounds, smoothedPreMid, juce::Colour(0x20FFFFFF), juce::Colour(0x06FFFFFF));
                drawSpectrum(g, bounds, smoothedPreSide, juce::Colour(0x20FFD93D), juce::Colour(0x06FFD93D));
                // Post-EQ: M=white, S=yellow
                drawSpectrum(g, bounds, smoothedPostMid, juce::Colour(0x60FFFFFF), juce::Colour(0x18FFFFFF));
                drawSpectrum(g, bounds, smoothedPostSide, juce::Colour(0x60FFD93D), juce::Colour(0x18FFD93D));
                break;
            }
        }

        // Draw EQ curves from cached data
        drawCachedEQCurve(g, bounds);

        // Draw individual band curves (subtle)
        int activeBands = processor.getActiveBandCount();
        for (int i = 0; i < activeBands; ++i)
            drawCachedBandCurve(g, bounds, i);

        // Draw draggable nodes on top
        for (int i = 0; i < activeBands; ++i)
            drawNode(g, bounds, i);

        // Draw border
        g.setColour(juce::Colour(0xFF333355));
        g.drawRect(bounds, 1.0f);

        // Draw spectrum mode legend in top-right corner (next to button)
        drawSpectrumLegend(g);
    }

    void resized() override
    {
        curveNeedsUpdate = true;
        // Place the spectrum mode button in top-right corner
        spectrumModeBtn.setBounds(getWidth() - 46, 4, 42, 20);
    }

    //==============================================================================
    void mouseDown(const juce::MouseEvent &e) override
    {
        dragBandIndex = hitTestNode(e.position);
        if (dragBandIndex >= 0)
            beginDrag(dragBandIndex, e.position);
    }

    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (dragBandIndex >= 0)
            performDrag(dragBandIndex, e.position);
    }

    void mouseUp(const juce::MouseEvent & /*e*/) override
    {
        dragBandIndex = -1;
    }

    void mouseMove(const juce::MouseEvent &e) override
    {
        int hit = hitTestNode(e.position);
        setMouseCursor(hit >= 0 ? juce::MouseCursor::DraggingHandCursor
                                : juce::MouseCursor::NormalCursor);
        hoveredBand = hit;
        repaint();
    }

    void mouseExit(const juce::MouseEvent & /*e*/) override
    {
        hoveredBand = -1;
        repaint();
    }

    // Scroll to adjust Q
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel) override
    {
        int hit = hitTestNode(e.position);
        if (hit >= 0)
        {
            auto prefix = "band" + juce::String(hit) + "_q";
            auto *param = processor.getAPVTS().getParameter(prefix);
            if (param != nullptr)
            {
                float currentNorm = param->getValue();
                float delta = wheel.deltaY * 0.05f;
                param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, currentNorm + delta));
            }
        }
    }

private:
    DynamicEQAudioProcessor &processor;

    // Spectrum display mode
    SpectrumDisplayMode spectrumDisplayMode = SpectrumDisplayMode::LR_Mix;
    juce::TextButton spectrumModeBtn;

    // Spectrum data — mono (L+R mix)
    std::array<float, SpectrumAnalyzer::fftSize / 2> preSpectrumData{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> postSpectrumData{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> smoothedPreSpectrum{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> smoothedPostSpectrum{};

    // Spectrum data — per-channel L/R
    std::array<float, SpectrumAnalyzer::fftSize / 2> preSpectrumDataL{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> preSpectrumDataR{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> postSpectrumDataL{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> postSpectrumDataR{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> smoothedPreL{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> smoothedPreR{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> smoothedPostL{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> smoothedPostR{};

    // Spectrum data — computed M/S (derived from L/R in GUI thread)
    std::array<float, SpectrumAnalyzer::fftSize / 2> smoothedPreMid{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> smoothedPreSide{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> smoothedPostMid{};
    std::array<float, SpectrumAnalyzer::fftSize / 2> smoothedPostSide{};

    // Cached EQ curve data (per-band magnitudes in dB, sampled at curveNumPoints)
    static constexpr int curveNumPoints = 1024;
    std::array<std::array<float, curveNumPoints>, DynamicEQAudioProcessor::numBands> cachedBandMagnitudes{};
    std::array<float, curveNumPoints> cachedTotalMagnitude{};
    std::array<double, curveNumPoints> curveFrequencies{};
    bool curveNeedsUpdate = true;

    // Track parameter changes for efficient curve update
    struct BandSnapshot
    {
        float freq = 0, gain = 0, q = 0, gr = 0;
        int type = 0;
        int channel = 0;  // ChannelMode index
        bool enabled = false, dynamic = false;
    };
    std::array<BandSnapshot, DynamicEQAudioProcessor::numBands> lastSnapshots{};

    int dragBandIndex = -1;
    int hoveredBand = -1;
    int lastActiveBandCount = -1;   // detect add/remove band

    // Relative drag state for frequency (log scale delta)
    // Absolute + bias drag state for gain (avoids jump and ensures full range)
    float dragStartMouseX = 0.0f;   // component-space X at drag start
    float dragStartFreq   = 0.0f;   // freq param at drag start
    float dragGainBias    = 0.0f;   // bias = startGainParam - yToDb(startMouseY), prevents jump

    static constexpr float nodeRadius = 10.0f;
    static constexpr float glowRadius = 22.0f;
    static constexpr float minFreqHz = 20.0f;
    static constexpr float maxFreqHz = 20000.0f;
    static constexpr float minDB = -24.0f;
    static constexpr float maxDB = 24.0f;

    //==============================================================================
    void timerCallback() override
    {
        // Process pre/post spectrum FFT (mono mix)
        auto &preSA = processor.getPreSpectrumAnalyzer();
        auto &postSA = processor.getPostSpectrumAnalyzer();

        if (preSA.isNewDataAvailable())
            preSA.processFFT(preSpectrumData);

        if (postSA.isNewDataAvailable())
            postSA.processFFT(postSpectrumData);

        // Process per-channel L/R spectrum FFT
        auto &preL = processor.getPreSpectrumL();
        auto &preR = processor.getPreSpectrumR();
        auto &postL = processor.getPostSpectrumL();
        auto &postR = processor.getPostSpectrumR();

        if (preL.isNewDataAvailable())
            preL.processFFT(preSpectrumDataL);
        if (preR.isNewDataAvailable())
            preR.processFFT(preSpectrumDataR);
        if (postL.isNewDataAvailable())
            postL.processFFT(postSpectrumDataL);
        if (postR.isNewDataAvailable())
            postR.processFFT(postSpectrumDataR);

        // Smooth the spectrum data
        const float attackSmooth  = 0.20f;
        const float releaseSmooth = 0.97f;

        auto smoothArray = [&](std::array<float, SpectrumAnalyzer::fftSize / 2>& smoothed,
                               const std::array<float, SpectrumAnalyzer::fftSize / 2>& raw)
        {
            for (size_t i = 0; i < smoothed.size(); ++i)
            {
                float coeff = raw[i] > smoothed[i] ? attackSmooth : releaseSmooth;
                smoothed[i] = coeff * smoothed[i] + (1.0f - coeff) * raw[i];
            }
        };

        smoothArray(smoothedPreSpectrum, preSpectrumData);
        smoothArray(smoothedPostSpectrum, postSpectrumData);
        smoothArray(smoothedPreL, preSpectrumDataL);
        smoothArray(smoothedPreR, preSpectrumDataR);
        smoothArray(smoothedPostL, postSpectrumDataL);
        smoothArray(smoothedPostR, postSpectrumDataR);

        // Compute M/S from L/R (approximate in magnitude domain)
        // M ≈ (L+R)/2, S ≈ |L-R|/2  (magnitudes, not complex — visual approximation)
        if (spectrumDisplayMode == SpectrumDisplayMode::MS_Split)
        {
            for (size_t i = 0; i < smoothedPreMid.size(); ++i)
            {
                smoothedPreMid[i]   = (smoothedPreL[i]  + smoothedPreR[i])  * 0.5f;
                smoothedPreSide[i]  = std::abs(smoothedPreL[i]  - smoothedPreR[i])  * 0.5f;
                smoothedPostMid[i]  = (smoothedPostL[i] + smoothedPostR[i]) * 0.5f;
                smoothedPostSide[i] = std::abs(smoothedPostL[i] - smoothedPostR[i]) * 0.5f;
            }
        }

        // Check if curve parameters changed
        checkAndUpdateCurve();

        repaint();
    }

    //==============================================================================
    // Check if any band parameters changed and recalculate curves if needed
    //==============================================================================
    void checkAndUpdateCurve()
    {
        auto &apvts = processor.getAPVTS();
        bool changed = curveNeedsUpdate;

        // Force rebuild whenever active band count changes
        int active = processor.getActiveBandCount();
        if (active != lastActiveBandCount)
        {
            lastActiveBandCount = active;
            changed = true;
        }

        for (int i = 0; i < active; ++i)
        {
            auto prefix = "band" + juce::String(i) + "_";
            BandSnapshot snap;
            snap.freq = apvts.getRawParameterValue(prefix + "freq")->load();
            snap.gain = apvts.getRawParameterValue(prefix + "gain")->load();
            snap.q = apvts.getRawParameterValue(prefix + "q")->load();
            snap.type = static_cast<int>(apvts.getRawParameterValue(prefix + "type")->load());
            snap.channel = static_cast<int>(apvts.getRawParameterValue(prefix + "channel")->load());
            snap.enabled = apvts.getRawParameterValue(prefix + "enabled")->load() > 0.5f;
            snap.dynamic = apvts.getRawParameterValue(prefix + "dynamic")->load() > 0.5f;
            snap.gr = snap.dynamic ? processor.getBandGainReduction(i) : 0.0f;

            auto &last = lastSnapshots[static_cast<size_t>(i)];
            if (snap.freq != last.freq || snap.gain != last.gain || snap.q != last.q || snap.type != last.type || snap.channel != last.channel || snap.enabled != last.enabled || snap.dynamic != last.dynamic || std::abs(snap.gr - last.gr) > 0.05f)
            {
                changed = true;
                last = snap;
            }
        }

        if (changed)
            rebuildCurveCache();
    }

    void rebuildCurveCache()
    {
        curveNeedsUpdate = false;
        const double sr = processor.getCurrentSampleRate();
        if (sr <= 0.0)
            return;

        const float width = static_cast<float>(getWidth());
        if (width <= 0.0f)
            return;

        // Build frequency array for sampling points
        for (int i = 0; i < curveNumPoints; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(curveNumPoints - 1);
            float x = t * width;
            curveFrequencies[static_cast<size_t>(i)] = static_cast<double>(xToFreq(x, width, minFreqHz, maxFreqHz));
        }

        // Reset total (start at 1.0 in linear for multiplicative accumulation)
        std::array<double, curveNumPoints> totalLinearMagnitude{};
        totalLinearMagnitude.fill(1.0);

        // Compute each band's magnitude response using JUCE's built-in method
        int activeBands = processor.getActiveBandCount();
        for (int b = 0; b < activeBands; ++b)
        {
            auto &snap = lastSnapshots[static_cast<size_t>(b)];
            auto &bandMag = cachedBandMagnitudes[static_cast<size_t>(b)];

            if (!snap.enabled)
            {
                bandMag.fill(0.0f);
                continue;
            }

            float effectiveGain = snap.gain;
            if (snap.dynamic)
                effectiveGain -= snap.gr;

            // Build filter coefficients once per band
            juce::dsp::IIR::Coefficients<float>::Ptr coeffs;
            float gainFactor = juce::Decibels::decibelsToGain(effectiveGain);

            switch (snap.type)
            {
            case 0:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, snap.freq, snap.q, gainFactor);
                break;
            case 1:
                coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, snap.freq, snap.q, gainFactor);
                break;
            case 2:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, snap.freq, snap.q, gainFactor);
                break;
            case 3: // Low Cut (High Pass)
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, snap.freq, snap.q);
                break;
            case 4: // High Cut (Low Pass)
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, snap.freq, snap.q);
                break;
            case 5: // Notch
            {
                // Standard biquad notch computed manually
                float w0    = juce::MathConstants<float>::twoPi * snap.freq / static_cast<float>(sr);
                float cosW0 = std::cos(w0);
                float alpha = std::sin(w0) / (2.0f * snap.q);
                coeffs = new juce::dsp::IIR::Coefficients<float> (
                    1.0f, -2.0f * cosW0, 1.0f,
                    1.0f + alpha, -2.0f * cosW0, 1.0f - alpha);
                break;
            }
            case 6: // Band Pass
                coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass(sr, snap.freq, snap.q);
                break;
            default:
                bandMag.fill(0.0f);
                continue;
            }

            if (coeffs == nullptr)
            {
                bandMag.fill(0.0f);
                continue;
            }

            // Use JUCE's batch magnitude computation (linear domain)
            std::array<double, curveNumPoints> magnitudes{};
            coeffs->getMagnitudeForFrequencyArray(curveFrequencies.data(), magnitudes.data(),
                                                  static_cast<size_t>(curveNumPoints), sr);

            for (int i = 0; i < curveNumPoints; ++i)
            {
                // Store per-band in dB (no premature floor clamping)
                float magDB = static_cast<float>(juce::Decibels::gainToDecibels(magnitudes[static_cast<size_t>(i)]));
                bandMag[static_cast<size_t>(i)] = magDB;

                // Accumulate total in LINEAR domain to avoid dB clamping artefacts
                totalLinearMagnitude[static_cast<size_t>(i)] *= magnitudes[static_cast<size_t>(i)];
            }
        }

        // Convert accumulated linear total back to dB
        for (int i = 0; i < curveNumPoints; ++i)
        {
            cachedTotalMagnitude[static_cast<size_t>(i)] =
                static_cast<float>(juce::Decibels::gainToDecibels(totalLinearMagnitude[static_cast<size_t>(i)]));
        }
    }

    //==============================================================================
    void drawGrid(juce::Graphics &g, juce::Rectangle<float> bounds)
    {
        g.setColour(juce::Colour(0x15FFFFFF));

        // Frequency grid lines (log scale)
        const float freqs[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
        for (float freq : freqs)
        {
            float x = freqToX(freq, bounds.getWidth(), minFreqHz, maxFreqHz);
            g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());

            g.setColour(juce::Colour(0x40FFFFFF));
            juce::String label;
            if (freq >= 1000.0f)
                label = juce::String(freq / 1000.0f, 0) + "k";
            else
                label = juce::String(static_cast<int>(freq));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(label, static_cast<int>(x) - 15, static_cast<int>(bounds.getBottom()) - 14, 30, 12,
                       juce::Justification::centred);
            g.setColour(juce::Colour(0x15FFFFFF));
        }

        // dB grid lines
        const float dbs[] = {-18, -12, -6, 0, 6, 12, 18};
        for (float db : dbs)
        {
            float y = dbToY(db, bounds.getHeight(), minDB, maxDB);
            g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());

            if (std::abs(db) > 0.1f)
            {
                g.setColour(juce::Colour(0x40FFFFFF));
                g.setFont(juce::FontOptions(10.0f));
                g.drawText(juce::String(static_cast<int>(db)) + " dB",
                           2, static_cast<int>(y) - 6, 40, 12, juce::Justification::left);
                g.setColour(juce::Colour(0x15FFFFFF));
            }
        }

        // 0 dB center line (brighter)
        float zeroY = dbToY(0.0f, bounds.getHeight(), minDB, maxDB);
        g.setColour(juce::Colour(0x30FFFFFF));
        g.drawHorizontalLine(static_cast<int>(zeroY), bounds.getX(), bounds.getRight());
    }

    //==============================================================================
    void drawSpectrum(juce::Graphics &g, juce::Rectangle<float> bounds,
                      const std::array<float, SpectrumAnalyzer::fftSize / 2> &data,
                      juce::Colour lineColour, juce::Colour fillColour)
    {
        const float width = bounds.getWidth();
        const float height = bounds.getHeight();
        const float sampleRate = static_cast<float>(processor.getCurrentSampleRate());
        if (sampleRate <= 0.0f)
            return;
        const int fftHalfSize = SpectrumAnalyzer::fftSize / 2;

        juce::Path spectrumPath;
        bool pathStarted = false;

        // Sample every 2 pixels for performance, then smooth via the path
        const int step = 2;
        for (int x = 0; x < static_cast<int>(width); x += step)
        {
            float freq = xToFreq(static_cast<float>(x), width, minFreqHz, maxFreqHz);
            int binIndex = static_cast<int>(freq / (sampleRate / static_cast<float>(SpectrumAnalyzer::fftSize)));
            binIndex = juce::jlimit(0, fftHalfSize - 1, binIndex);

            float magnitude = data[static_cast<size_t>(binIndex)];
            float y = juce::jmap(magnitude, 0.0f, 1.0f, height, 0.0f);

            if (!pathStarted)
            {
                spectrumPath.startNewSubPath(static_cast<float>(x), y);
                pathStarted = true;
            }
            else
            {
                spectrumPath.lineTo(static_cast<float>(x), y);
            }
        }

        if (pathStarted)
        {
            // Ensure path extends to right edge
            spectrumPath.lineTo(width, spectrumPath.getCurrentPosition().y);

            // Create fill path
            juce::Path fillPath(spectrumPath);
            fillPath.lineTo(width, height);
            fillPath.lineTo(0.0f, height);
            fillPath.closeSubPath();

            // Gradient fill
            juce::ColourGradient gradient(fillColour.withAlpha(0.4f), 0.0f, 0.0f,
                                          fillColour.withAlpha(0.0f), 0.0f, height, false);
            g.setGradientFill(gradient);
            g.fillPath(fillPath);

            // Stroke
            g.setColour(lineColour);
            g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));
        }
    }

    //==============================================================================
    void drawCachedEQCurve(juce::Graphics &g, juce::Rectangle<float> bounds)
    {
        const float width = bounds.getWidth();
        const float height = bounds.getHeight();

        juce::Path curvePath;

        for (int i = 0; i < curveNumPoints; ++i)
        {
            float x = static_cast<float>(i) / static_cast<float>(curveNumPoints - 1) * width;
            float totalDB = juce::jlimit(minDB, maxDB, cachedTotalMagnitude[static_cast<size_t>(i)]);
            float y = dbToY(totalDB, height, minDB, maxDB);

            if (i == 0)
                curvePath.startNewSubPath(x, y);
            else
                curvePath.lineTo(x, y);
        }

        // Fill area between curve and 0dB line
        float zeroY = dbToY(0.0f, height, minDB, maxDB);
        juce::Path fillPath(curvePath);
        fillPath.lineTo(width, zeroY);
        fillPath.lineTo(0.0f, zeroY);
        fillPath.closeSubPath();

        g.setColour(juce::Colour(0x18FFFFFF));
        g.fillPath(fillPath);

        // Stroke the curve
        g.setColour(juce::Colour(0xBBFFFFFF));
        g.strokePath(curvePath, juce::PathStrokeType(2.0f));
    }

    //==============================================================================
    void drawSpectrumLegend(juce::Graphics &g)
    {
        // Draw a small legend in the top-right area showing current mode's colors
        const int legendX = getWidth() - 140;
        const int legendY = 6;
        g.setFont(juce::FontOptions(9.0f));

        switch (spectrumDisplayMode)
        {
            case SpectrumDisplayMode::LR_Mix:
                g.setColour(juce::Colour(0x80FFFFFF));
                g.drawText("Pre", legendX, legendY, 25, 10, juce::Justification::centredRight);
                g.setColour(juce::Colour(0x8000D4FF));
                g.drawText("Post", legendX + 28, legendY, 25, 10, juce::Justification::centredRight);
                break;
            case SpectrumDisplayMode::LR_Split:
                g.setColour(juce::Colour(0xFF4D96FF));
                g.drawText("L", legendX, legendY, 15, 10, juce::Justification::centred);
                g.setColour(juce::Colour(0xFFFF6B6B));
                g.drawText("R", legendX + 18, legendY, 15, 10, juce::Justification::centred);
                break;
            case SpectrumDisplayMode::MS_Split:
                g.setColour(juce::Colour(0xFFFFFFFF));
                g.drawText("M", legendX, legendY, 15, 10, juce::Justification::centred);
                g.setColour(juce::Colour(0xFFFFD93D));
                g.drawText("S", legendX + 18, legendY, 15, 10, juce::Justification::centred);
                break;
        }
    }

    //==============================================================================
    void drawCachedBandCurve(juce::Graphics &g, juce::Rectangle<float> bounds, int bandIndex)
    {
        auto &snap = lastSnapshots[static_cast<size_t>(bandIndex)];
        if (!snap.enabled)
            return;

        const float width = bounds.getWidth();
        const float height = bounds.getHeight();
        auto &bandMag = cachedBandMagnitudes[static_cast<size_t>(bandIndex)];

        juce::Path bandPath;
        juce::Colour colour = getBandColour(bandIndex).withAlpha(0.3f);

        for (int i = 0; i < curveNumPoints; ++i)
        {
            float x = static_cast<float>(i) / static_cast<float>(curveNumPoints - 1) * width;
            float y = dbToY(juce::jlimit(minDB, maxDB, bandMag[static_cast<size_t>(i)]), height, minDB, maxDB);

            if (i == 0)
                bandPath.startNewSubPath(x, y);
            else
                bandPath.lineTo(x, y);
        }

        // Fill area between band curve and 0dB
        float zeroY = dbToY(0.0f, height, minDB, maxDB);
        juce::Path fillPath(bandPath);
        fillPath.lineTo(width, zeroY);
        fillPath.lineTo(0.0f, zeroY);
        fillPath.closeSubPath();

        g.setColour(getBandColour(bandIndex).withAlpha(0.06f));
        g.fillPath(fillPath);

        g.setColour(colour);
        // Use dashed stroke for non-Stereo channel modes
        if (lastSnapshots[static_cast<size_t>(bandIndex)].channel > 0)
        {
            juce::Path dashedPath;
            float dashLengths[] = { 4.0f, 3.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashedPath, bandPath, dashLengths, 2);
            g.fillPath(dashedPath);
        }
        else
        {
            g.strokePath(bandPath, juce::PathStrokeType(1.0f));
        }
    }

    //==============================================================================
    void drawNode(juce::Graphics &g, juce::Rectangle<float> bounds, int bandIndex)
    {
        auto &apvts = processor.getAPVTS();
        auto prefix = "band" + juce::String(bandIndex) + "_";
        bool enabled = apvts.getRawParameterValue(prefix + "enabled")->load() > 0.5f;
        if (!enabled)
            return;

        float freq = apvts.getRawParameterValue(prefix + "freq")->load();
        float gain = apvts.getRawParameterValue(prefix + "gain")->load();
        float gainReduction = processor.getBandGainReduction(bandIndex);
        int   filterType    = static_cast<int>(apvts.getRawParameterValue(prefix + "type")->load());

        // LowCut/HighCut/Notch/BandPass (type >= 3) have no gain meaning — place node at 0 dB
        const bool isGainless = (filterType >= 3);
        float displayGain = isGainless ? 0.0f : (gain - gainReduction);

        float x = freqToX(freq, bounds.getWidth(), minFreqHz, maxFreqHz);
        float y = dbToY(displayGain, bounds.getHeight(), minDB, maxDB);

        juce::Colour colour = getBandColour(bandIndex);
        bool isHovered = (hoveredBand == bandIndex);
        bool isDragged = (dragBandIndex == bandIndex);

        // Outer glow
        float currentGlowRadius = glowRadius + (isHovered ? 6.0f : 0.0f) + (isDragged ? 8.0f : 0.0f);
        juce::ColourGradient glow(colour.withAlpha(0.4f), x, y,
                                  colour.withAlpha(0.0f), x + currentGlowRadius, y, true);
        g.setGradientFill(glow);
        g.fillEllipse(x - currentGlowRadius, y - currentGlowRadius,
                      currentGlowRadius * 2.0f, currentGlowRadius * 2.0f);

        // Gain reduction indicator line (when compression active, gain-based types only)
        if (!isGainless && gainReduction > 0.1f)
        {
            float staticY = dbToY(gain, bounds.getHeight(), minDB, maxDB);
            g.setColour(colour.withAlpha(0.5f));
            juce::Path reductionLine;
            reductionLine.startNewSubPath(x, staticY);
            reductionLine.lineTo(x, y);
            g.strokePath(reductionLine, juce::PathStrokeType(1.5f));

            // Draw small GR text
            g.setFont(juce::FontOptions(9.0f));
            g.setColour(colour.withAlpha(0.8f));
            g.drawText("-" + juce::String(gainReduction, 1) + " dB",
                       static_cast<int>(x) + 12, static_cast<int>((staticY + y) / 2.0f) - 6,
                       50, 12, juce::Justification::left);
        }

        // Node circle (filled)
        float currentRadius = nodeRadius + (isHovered ? 2.0f : 0.0f) + (isDragged ? 3.0f : 0.0f);
        g.setColour(colour);
        g.fillEllipse(x - currentRadius, y - currentRadius, currentRadius * 2.0f, currentRadius * 2.0f);

        // Inner ring
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawEllipse(x - currentRadius + 2.0f, y - currentRadius + 2.0f,
                      (currentRadius - 2.0f) * 2.0f, (currentRadius - 2.0f) * 2.0f, 1.5f);

        // Band label
        g.setFont(juce::FontOptions(11.0f));
        g.setColour(juce::Colours::white);
        g.drawText(juce::String(bandIndex + 1),
                   static_cast<int>(x) - 5, static_cast<int>(y) - 5, 10, 10,
                   juce::Justification::centred);

        // Channel mode label (show below node for non-Stereo modes)
        int channelMode = static_cast<int>(apvts.getRawParameterValue(prefix + "channel")->load());
        if (channelMode > 0)
        {
            const char* modeLabels[] = { "", "L", "R", "M", "S" };
            g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
            g.setColour(colour.withAlpha(0.9f));
            g.drawText(modeLabels[channelMode],
                       static_cast<int>(x) - 8, static_cast<int>(y) + static_cast<int>(currentRadius) + 1, 16, 10,
                       juce::Justification::centred);
        }
    }

    //==============================================================================
    int hitTestNode(juce::Point<float> pos)
    {
        auto &apvts = processor.getAPVTS();
        float width  = static_cast<float>(getWidth());
        float height = static_cast<float>(getHeight());

        for (int i = 0; i < processor.getActiveBandCount(); ++i)
        {
            auto prefix = "band" + juce::String(i) + "_";
            bool enabled = apvts.getRawParameterValue(prefix + "enabled")->load() > 0.5f;
            if (!enabled)
                continue;

            float freq = apvts.getRawParameterValue(prefix + "freq")->load();
            float gain = apvts.getRawParameterValue(prefix + "gain")->load();
            float gr   = processor.getBandGainReduction(i);
            int   type = static_cast<int>(apvts.getRawParameterValue(prefix + "type")->load());

            // Gainless types always sit at 0 dB visually
            float displayGain = (type >= 3) ? 0.0f : (gain - gr);

            float nx = freqToX(freq, width, minFreqHz, maxFreqHz);
            float ny = dbToY(displayGain, height, minDB, maxDB);

            float dist = pos.getDistanceFrom({nx, ny});
            if (dist <= glowRadius)
                return i;
        }
        return -1;
    }

    void beginDrag(int bandIndex, juce::Point<float> mousePos)
    {
        auto &apvts = processor.getAPVTS();
        auto prefix = "band" + juce::String(bandIndex) + "_";
        float height = static_cast<float>(getHeight());

        dragStartMouseX = mousePos.x;
        dragStartFreq   = apvts.getRawParameterValue(prefix + "freq")->load();

        // Gain bias: captures the difference between the actual gain param and what the
        // absolute mouse Y maps to (accounts for GR offset), so the node never jumps at drag start.
        float startGain  = apvts.getRawParameterValue(prefix + "gain")->load();
        float clampedY   = juce::jlimit(0.0f, height, mousePos.y);
        dragGainBias     = startGain - yToDb(clampedY, height, minDB, maxDB);
    }

    void performDrag(int bandIndex, juce::Point<float> pos)
    {
        auto &apvts = processor.getAPVTS();
        auto prefix = "band" + juce::String(bandIndex) + "_";
        float width  = static_cast<float>(getWidth());
        float height = static_cast<float>(getHeight());

        // ---- Frequency: delta X in log domain ----
        float startX  = freqToX(dragStartFreq, width, minFreqHz, maxFreqHz);
        float targetX = juce::jlimit(0.0f, width, startX + (pos.x - dragStartMouseX));
        float freq    = juce::jlimit(minFreqHz, maxFreqHz, xToFreq(targetX, width, minFreqHz, maxFreqHz));

        auto *freqParam = apvts.getParameter(prefix + "freq");
        if (freqParam != nullptr)
            freqParam->setValueNotifyingHost(freqParam->getNormalisableRange().convertTo0to1(freq));

        // ---- Gain: absolute Y mapping + bias, pos.y clamped to component bounds ----
        int filterType = static_cast<int>(apvts.getRawParameterValue(prefix + "type")->load());
        if (filterType < 3)
        {
            float clampedY = juce::jlimit(0.0f, height, pos.y);
            float gain     = juce::jlimit(minDB, maxDB, yToDb(clampedY, height, minDB, maxDB) + dragGainBias);

            auto *gainParam = apvts.getParameter(prefix + "gain");
            if (gainParam != nullptr)
                gainParam->setValueNotifyingHost(gainParam->getNormalisableRange().convertTo0to1(gain));
        }
        // LowCut/HighCut/Notch/BandPass: Y drag does nothing
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumComponent)
};
