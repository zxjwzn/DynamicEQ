/*
  ==============================================================================

    SpectrumAnalyzer.h
    FFT-based spectrum analyzer with FIFO for real-time display

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
// Lock-free FIFO for pushing audio samples from audio thread to GUI thread
//==============================================================================
template <int FifoSize>
struct AudioFifo
{
    void prepare (int numSamples)
    {
        for (auto& buf : buffers)
            buf.setSize (1, numSamples, false, true, true);

        fifo.setTotalSize (FifoSize);
        prepared.set (true);
    }

    bool push (const juce::AudioBuffer<float>& buffer)
    {
        if (! prepared.get())
            return false;

        jassert (buffer.getNumSamples() <= buffers[0].getNumSamples());

        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 > 0)
            buffers[static_cast<size_t> (start1)].makeCopyOf (buffer, true);

        fifo.finishedWrite (size1 + size2);
        return size1 > 0;
    }

    bool pull (juce::AudioBuffer<float>& buffer)
    {
        if (! prepared.get())
            return false;

        int start1, size1, start2, size2;
        fifo.prepareToRead (1, start1, size1, start2, size2);

        if (size1 > 0)
            buffer.makeCopyOf (buffers[static_cast<size_t> (start1)], true);

        fifo.finishedRead (size1 + size2);
        return size1 > 0;
    }

    int getNumAvailable() const { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo { FifoSize };
    std::array<juce::AudioBuffer<float>, FifoSize> buffers;
    juce::Atomic<bool> prepared { false };
};

//==============================================================================
// Single-channel FFT data producer
//==============================================================================
class SpectrumAnalyzer
{
public:
    static constexpr int fftOrder = 12;            // 4096-point FFT
    static constexpr int fftSize  = 1 << fftOrder; // 4096
    // Trigger a new FFT every hopSize samples for higher visual update rate.
    // hopSize=512 at 44100 Hz => ~86 FFT updates/sec (vs. ~10 before)
    static constexpr int hopSize  = 512;

    SpectrumAnalyzer()
        : fft (fftOrder),
          window (static_cast<size_t> (fftSize), juce::dsp::WindowingFunction<float>::hann)
    {
        circularBuffer.fill (0.0f);
        fftData.fill (0.0f);
        renderBuffer.fill (0.0f);
    }

    void pushSamples (const float* data, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            circularBuffer[static_cast<size_t> (writePos)] = data[i];
            writePos = (writePos + 1) & (fftSize - 1);  // fftSize is power-of-2, bit-mask is safe
            ++hopCounter;

            if (hopCounter >= hopSize)
            {
                hopCounter = 0;
                // writePos now points to the oldest slot → copy fftSize samples in order
                for (int j = 0; j < fftSize; ++j)
                    fftData[static_cast<size_t> (j)] =
                        circularBuffer[static_cast<size_t> ((writePos + j) & (fftSize - 1))];
                newFFTDataAvailable.set (true);
            }
        }
    }

    // Call from GUI thread to check if new data is available
    bool isNewDataAvailable() const { return newFFTDataAvailable.get(); }

    // Process FFT on GUI thread, fills magnitudeDB array
    void processFFT (std::array<float, fftSize / 2>& magnitudeDB, float minDB = -100.0f, float maxDB = 0.0f)
    {
        if (! newFFTDataAvailable.compareAndSetBool (false, true))
            return;

        // Copy fftData snapshot to renderBuffer so audio thread can immediately overwrite fftData
        std::copy (fftData.begin(), fftData.end(), renderBuffer.begin());

        window.multiplyWithWindowingTable (renderBuffer.data(), static_cast<size_t> (fftSize));
        fft.performFrequencyOnlyForwardTransform (renderBuffer.data());

        const float scaleFactor = 1.0f / static_cast<float> (fftSize);

        for (int i = 0; i < fftSize / 2; ++i)
        {
            float magnitude = renderBuffer[static_cast<size_t> (i)] * scaleFactor;
            float db = juce::Decibels::gainToDecibels (magnitude, minDB);
            magnitudeDB[static_cast<size_t> (i)] = juce::jmap (db, minDB, maxDB, 0.0f, 1.0f);
        }
    }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    // Ring buffer holding the last fftSize samples for overlap-based FFT
    std::array<float, fftSize>     circularBuffer {};   // written by audio thread
    std::array<float, fftSize * 2> fftData        {};   // captured snapshot (audio thread writes, shared)
    std::array<float, fftSize * 2> renderBuffer   {};   // GUI-thread-only work copy for FFT/window
    int writePos   = 0;   // next write slot in circularBuffer, always in [0, fftSize-1]
    int hopCounter = 0;   // counts new samples since last FFT trigger

    juce::Atomic<bool> newFFTDataAvailable { false };
};
