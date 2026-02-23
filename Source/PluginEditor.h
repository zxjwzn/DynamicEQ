/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/SpectrumComponent.h"

//==============================================================================
// A compact band control strip for one EQ band
//==============================================================================
class BandControlStrip : public juce::Component
{
public:
    BandControlStrip (DynamicEQAudioProcessor& p, int bandIndex);
    ~BandControlStrip() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    int bandIndex;
    juce::Colour bandColour;

    juce::Slider freqSlider, gainSlider, qSlider;
    juce::Slider thresholdSlider, ratioSlider;
    juce::Slider attackSlider, releaseSlider;
    juce::ToggleButton enableBtn;
    juce::ToggleButton dynamicBtn;
    juce::ComboBox typeCombo;

    // Label as text
    juce::Label freqLabel, gainLabel, qLabel;
    juce::Label threshLabel, ratioLabel;
    juce::Label attackLabel, releaseLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> freqAtt, gainAtt, qAtt;
    std::unique_ptr<SliderAttachment> threshAtt, ratioAtt, attackAtt, releaseAtt;
    std::unique_ptr<ButtonAttachment> enableAtt, dynamicAtt;
    std::unique_ptr<ComboAttachment>  typeAtt;

    void setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandControlStrip)
};

//==============================================================================
class DynamicEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::ScrollBar::Listener
{
public:
    DynamicEQAudioProcessorEditor (DynamicEQAudioProcessor&);
    ~DynamicEQAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // ScrollBar::Listener override (drives controlViewport from nav bar scrollbar)
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void updateNavScrollBar();   // sync scrollbar range/thumb with viewport state

    DynamicEQAudioProcessor& audioProcessor;

    SpectrumComponent spectrumComponent;

    // Control area: a Viewport wraps a plain container so strips can scroll horizontally
    juce::Component  controlContainer;   // holds the band strips
    juce::Viewport   controlViewport;    // provides horizontal scrolling

    // Declared AFTER controlContainer/controlViewport so they destruct first
    juce::OwnedArray<BandControlStrip> bandStrips;

    // Nav bar widgets
    juce::TextButton addBandBtn    { "+" };
    juce::TextButton removeBandBtn { "-" };
    juce::TextButton collapseBtn;           // ▼ / ▲
    juce::ScrollBar  navScrollBar  { false }; // horizontal scrollbar in nav bar

    // Layout state
    bool controlAreaCollapsed = false;
    juce::Rectangle<int> navBarBounds;      // saved for paint()

    static constexpr int navBarH    = 28;
    static constexpr int controlH   = 290;
    static constexpr int stripMinW  = 220;   // minimum strip width (triggers scroll)
    static constexpr int stripMaxW  = 250;   // maximum strip width (prevents over-stretch)

    void updateBandVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicEQAudioProcessorEditor)
};
