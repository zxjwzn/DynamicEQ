/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Marker class to identify slider text boxes for custom drawing
//==============================================================================
struct SliderTextBoxLabel : public juce::Label
{
    SliderTextBoxLabel() = default;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliderTextBoxLabel)
};

//==============================================================================
// Custom LookAndFeel for dark-themed sliders
//==============================================================================
class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel()
    {
        setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF252540));
        setColour (juce::Slider::thumbColourId, juce::Colour (0xFFCCCCFF)); // Used for the dot/pointer
        setColour (juce::Slider::trackColourId, juce::Colour (0xFF4D96FF)); // Used for the arc
        
        // Unused by custom drawRotarySlider but kept for reference
        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFF4D96FF));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xFF333355));
        
        setColour (juce::Label::textColourId, juce::Colour (0xFFCCCCDD));
        setColour (juce::ToggleButton::textColourId, juce::Colour (0xFFCCCCDD));
        setColour (juce::ToggleButton::tickColourId, juce::Colour (0xFF4D96FF));
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xFF252540));
        setColour (juce::ComboBox::textColourId, juce::Colour (0xFFCCCCDD));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xFF333355));
        setColour (juce::ComboBox::arrowColourId, juce::Colour (0xFF9999BB));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xFF1A1A2E));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xFFCCCCDD));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xFF4D96FF));
        setColour (juce::TextButton::buttonColourId,                juce::Colour (0xFF252545));
        setColour (juce::TextButton::buttonOnColourId,              juce::Colour (0xFF4D96FF));
        setColour (juce::TextButton::textColourOffId,               juce::Colour (0xFFCCCCDD));
        setColour (juce::TextButton::textColourOnId,                juce::Colours::white);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) override
    {
        auto outline = slider.findColour (juce::Slider::rotarySliderOutlineColourId);
        auto fill    = slider.findColour (juce::Slider::rotarySliderFillColourId);

        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10);

        auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto lineW = 3.0f; // Thickness of the arc
        auto arcRadius = radius - lineW * 0.5f;

        juce::Path backgroundArc;
        backgroundArc.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f,
                                     rotaryStartAngle, rotaryEndAngle, true);

        g.setColour (outline);
        g.strokePath (backgroundArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (slider.isEnabled())
        {
            juce::Path valueArc;
            valueArc.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f,
                                    rotaryStartAngle, toAngle, true);

            g.setColour (fill);
            g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Draw a simple pointer/dot
        // juce::Point<float> thumbPoint (bounds.getCentreX() + arcRadius * std::cos (toAngle - juce::MathConstants<float>::halfPi),
        //                                bounds.getCentreY() + arcRadius * std::sin (toAngle - juce::MathConstants<float>::halfPi));
        
        // Or a classic pointer needle style inside
        juce::Path p;
        auto pointerLength = radius * 0.7f;
        auto pointerThickness = 3.0f;
        p.addRectangle (-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
        p.applyTransform (juce::AffineTransform::rotation (toAngle).translated (bounds.getCentreX(), bounds.getCentreY()));
        g.setColour (juce::Colours::white.withAlpha(0.8f));
        g.fillPath (p);
    }

    juce::Label* createSliderTextBox (juce::Slider&) override
    {
        auto* l = new SliderTextBoxLabel();
        l->setJustificationType (juce::Justification::centred);
        l->setKeyboardType (juce::TextInputTarget::decimalKeyboard);
        l->setColour (juce::Label::textColourId,            juce::Colour (0xFFCCDDEE));
        l->setColour (juce::Label::backgroundColourId,      juce::Colours::transparentBlack);
        l->setColour (juce::Label::outlineColourId,         juce::Colours::transparentBlack);
        l->setColour (juce::TextEditor::textColourId,       juce::Colour (0xFFCCDDEE));
        l->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xFF1A1A30));
        l->setColour (juce::TextEditor::highlightColourId,  juce::Colour (0xFF4D96FF).withAlpha (0.4f));
        return l;
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        // Only apply custom drawing to slider text boxes
        if (dynamic_cast<SliderTextBoxLabel*> (&label) == nullptr)
        {
            LookAndFeel_V4::drawLabel (g, label);
            return;
        }

        auto b = label.getLocalBounds().toFloat();

        // Dark semi-transparent rounded background
        g.setColour (juce::Colour (0xCC141428));
        g.fillRoundedRectangle (b.reduced (0.5f), 3.0f);

        // Subtle border
        g.setColour (juce::Colour (0xFF3A3A60));
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 0.8f);

        if (!label.isBeingEdited())
        {
            auto textAlpha = label.isEnabled() ? 1.0f : 0.5f;
            g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (textAlpha));
            g.setFont (juce::FontOptions (11.5f));
            g.drawFittedText (label.getText(), label.getLocalBounds().reduced (3, 1),
                              juce::Justification::centred, 1, 1.0f);
        }
    }

    // Called when the TextEditor inside a Label is shown (editing mode)
    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& te) override
    {
        // Only apply custom style if this editor belongs to one of our slider text boxes
        if (dynamic_cast<SliderTextBoxLabel*> (te.getParentComponent()) != nullptr)
        {
            g.setColour (juce::Colour (0xFF0F0F22));
            g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 3.0f);
        }
        else
        {
            LookAndFeel_V4::fillTextEditorBackground (g, width, height, te);
        }
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& te) override
    {
        // Only apply custom style if this editor belongs to one of our slider text boxes
        if (dynamic_cast<SliderTextBoxLabel*> (te.getParentComponent()) != nullptr)
        {
            // Bright blue border when actively editing
            g.setColour (juce::Colour (0xFF4D96FF).withAlpha (0.85f));
            g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 3.0f, 1.2f);
        }
        else
        {
            LookAndFeel_V4::drawTextEditorOutline (g, width, height, te);
        }
    }

    // ComboBox with dynamic arrow size proportional to component height
    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override
    {
        juce::ignoreUnused (buttonX, buttonY, buttonW, buttonH);
        const float corner = juce::jmin (4.0f, static_cast<float> (height) * 0.3f);
        const juce::Rectangle<float> b (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));

        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (b, corner);

        g.setColour (isButtonDown ? juce::Colour (0xFF4D96FF)
                                  : box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);

        // Arrow: scale with height
        const float arrowH = static_cast<float> (height) * 0.28f;
        const float arrowW = arrowH * 1.5f;
        const float cx = static_cast<float> (width) - static_cast<float> (height) * 0.5f;
        const float cy = static_cast<float> (height) * 0.5f;

        juce::Path arrow;
        arrow.addTriangle (cx - arrowW * 0.5f, cy - arrowH * 0.35f,
                           cx + arrowW * 0.5f, cy - arrowH * 0.35f,
                           cx,                 cy + arrowH * 0.65f);

        g.setColour (box.findColour (juce::ComboBox::arrowColourId)
                       .withAlpha (isButtonDown ? 1.0f : 0.8f));
        g.fillPath (arrow);
    }

    // ---- Popup menu (ComboBox dropdown) dark style -------------------------
    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        // fillAll first to eliminate white corner pixels (popup window is a rectangle)
        g.setColour (juce::Colour (0xFF16162B));
        g.fillAll();
        // Rounded border on top (visual only, corners are already dark)
        g.fillRoundedRectangle (0.0f, 0.0f, (float)width, (float)height, 5.0f);
        g.setColour (juce::Colour (0xFF3A3A60));
        g.drawRoundedRectangle (0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f, 5.0f, 1.0f);
    }

    int getPopupMenuBorderSize() override { return 4; }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool /*isTicked*/, bool /*hasSubMenu*/,
                            const juce::String& text,
                            const juce::String& /*shortcutKeyText*/,
                            const juce::Drawable* /*icon*/,
                            const juce::Colour* /*textColour*/) override
    {
        if (isSeparator)
        {
            g.setColour (juce::Colour (0xFF2D2D55));
            g.fillRect (area.withHeight(1).withY (area.getCentreY()));
            return;
        }

        const bool isDimmed = !isActive;
        juce::Rectangle<float> r (area.toFloat().reduced (2.0f, 1.0f));

        if (isHighlighted && isActive)
        {
            g.setColour (juce::Colour (0xFF2A3A5A));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (juce::Colour (0xFF4D96FF).withAlpha (0.5f));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 0.8f);
        }

        g.setColour (isDimmed ? juce::Colour (0xFF555577)
                              : isHighlighted ? juce::Colour (0xFFDDEEFF)
                                              : juce::Colour (0xFFAABBCC));
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (text, r.reduced (6.0f, 0.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1);
    }

    // ---- Scrollbar (dark themed) ----------------------------------------
    void drawScrollbar (juce::Graphics& g,
                        juce::ScrollBar& /*scrollbar*/,
                        int x, int y, int width, int height,
                        bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override
    {
        const juce::Rectangle<float> track ((float)x, (float)y, (float)width, (float)height);
        g.setColour (juce::Colour (0xFF1A1A2E));
        g.fillRoundedRectangle (track, 4.0f);

        if (thumbSize > 0)
        {
            juce::Rectangle<float> thumb;
            if (isScrollbarVertical)
                thumb = { (float)x + 2, (float)(y + thumbStartPosition) + 2,
                          (float)width - 4, (float)thumbSize - 4 };
            else
                thumb = { (float)(x + thumbStartPosition) + 2, (float)y + 2,
                          (float)thumbSize - 4, (float)height - 4 };

            const float alpha = isMouseDown ? 0.9f : (isMouseOver ? 0.7f : 0.5f);
            g.setColour (juce::Colour (0xFF5A9FD4).withAlpha (alpha));
            g.fillRoundedRectangle (thumb, 3.0f);
        }
    }
};

static DarkLookAndFeel& getDarkLookAndFeel()
{
    static DarkLookAndFeel lnf;
    return lnf;
}

//==============================================================================
// BandControlStrip implementation
//==============================================================================
BandControlStrip::BandControlStrip (DynamicEQAudioProcessor& p, int index)
    : bandIndex (index), bandColour (getBandColour (index))
{
    auto& apvts = p.getAPVTS();
    auto prefix = "band" + juce::String (bandIndex) + "_";

    setLookAndFeel (&getDarkLookAndFeel());

    // Type combo — must match the StringArray order in PluginProcessor::createParameterLayout()
    typeCombo.addItem (juce::String::fromUTF8 ("\u4f4e\u67b6"),     1);  // Low Shelf
    typeCombo.addItem (juce::String::fromUTF8 ("\u5cf0\u503c"),     2);  // Peak
    typeCombo.addItem (juce::String::fromUTF8 ("\u9ad8\u67b6"),     3);  // High Shelf
    typeCombo.addItem (juce::String::fromUTF8 ("\u4f4e\u622a"),     4);  // Low Cut  (HP)
    typeCombo.addItem (juce::String::fromUTF8 ("\u9ad8\u622a"),     5);  // High Cut (LP)
    typeCombo.addItem (juce::String::fromUTF8 ("\u964d\u5236"),     6);  // Notch
    typeCombo.addItem (juce::String::fromUTF8 ("\u5e26\u901a"),     7);  // Band Pass
    addAndMakeVisible (typeCombo);
    typeAtt = std::make_unique<ComboAttachment> (apvts, prefix + "type", typeCombo);

    // Enable/Dynamic toggles
    enableBtn.setButtonText (juce::String::fromUTF8 ("\u542f\u7528"));
    dynamicBtn.setButtonText (juce::String::fromUTF8 ("\u52a8\u6001"));
    addAndMakeVisible (enableBtn);
    addAndMakeVisible (dynamicBtn);
    enableAtt  = std::make_unique<ButtonAttachment> (apvts, prefix + "enabled", enableBtn);
    dynamicAtt = std::make_unique<ButtonAttachment> (apvts, prefix + "dynamic", dynamicBtn);

    // Sliders
    setupSlider (freqSlider,      freqLabel,    juce::String::fromUTF8 ("\u9891\u7387"));
    setupSlider (gainSlider,      gainLabel,    juce::String::fromUTF8 ("\u589e\u76ca"));
    setupSlider (qSlider,         qLabel,       juce::String::fromUTF8 ("Q\u503c"));
    setupSlider (thresholdSlider, threshLabel,  juce::String::fromUTF8 ("\u9608\u503c"));
    setupSlider (ratioSlider,     ratioLabel,   juce::String::fromUTF8 ("\u6bd4\u7387"));
    setupSlider (attackSlider,    attackLabel,   juce::String::fromUTF8 ("\u8d77\u97f3"));
    setupSlider (releaseSlider,   releaseLabel,  juce::String::fromUTF8 ("\u91ca\u653e"));

    // Attachments
    freqAtt    = std::make_unique<SliderAttachment> (apvts, prefix + "freq",      freqSlider);
    gainAtt    = std::make_unique<SliderAttachment> (apvts, prefix + "gain",      gainSlider);
    qAtt       = std::make_unique<SliderAttachment> (apvts, prefix + "q",         qSlider);
    threshAtt  = std::make_unique<SliderAttachment> (apvts, prefix + "threshold", thresholdSlider);
    ratioAtt   = std::make_unique<SliderAttachment> (apvts, prefix + "ratio",     ratioSlider);
    attackAtt  = std::make_unique<SliderAttachment> (apvts, prefix + "attack",    attackSlider);
    releaseAtt = std::make_unique<SliderAttachment> (apvts, prefix + "release",   releaseSlider);

    // Slider suffix
    freqSlider.setTextValueSuffix (" Hz");
    gainSlider.setTextValueSuffix (" dB");
    thresholdSlider.setTextValueSuffix (" dB");
    attackSlider.setTextValueSuffix (" ms");
    releaseSlider.setTextValueSuffix (" ms");
}

void BandControlStrip::setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    // Initial textbox setup - will be overridden in resized for width adaptation
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (13.0f)); // Slightly reduced from 14 to save vertical space if needed visually
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, juce::Colour (0xFFAABBCC));
    addAndMakeVisible (label);
}

void BandControlStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background with subtle band colour tint
    g.setColour (juce::Colour (0xFF16162B));
    g.fillRoundedRectangle (bounds, 6.0f);

    // Top colour strip
    g.setColour (bandColour);
    g.fillRoundedRectangle (bounds.removeFromTop (3.0f), 2.0f);

    // Band number
    g.setColour (bandColour.withAlpha (0.6f));
    g.setFont (juce::FontOptions (15.0f).withStyle ("Bold"));
    g.drawText (juce::String::fromUTF8 ("\u9891\u6bb5 ") + juce::String (bandIndex + 1), getLocalBounds().removeFromTop (24),
                juce::Justification::centred);
}

void BandControlStrip::resized()
{
    auto bounds = getLocalBounds().reduced (4);
    bounds.removeFromTop (24); // Title area

    // Top row: enable, type, dynamic
    auto topRow = bounds.removeFromTop (26);
    enableBtn.setBounds (topRow.removeFromLeft (58));
    dynamicBtn.setBounds (topRow.removeFromRight (58));
    auto comboW = juce::jmin (100, topRow.getWidth());
    typeCombo.setBounds (topRow.withSizeKeepingCentre (comboW, topRow.getHeight()));

    bounds.removeFromTop (3);

    // Dynamic row calculation
    int availableHeight = bounds.getHeight();
    int rowCount = 3;
    int rowHeight = availableHeight / rowCount;
    int labelHeight = 15;

    auto layoutKnob = [&](juce::Slider& slider, juce::Label& label, juce::Rectangle<int> area)
    {
        label.setBounds (area.removeFromTop (labelHeight));
        // Increase knob size by using the full available width/height and slightly negative reduction if needed, or just tight bounds
        slider.setBounds (area); 
        
        // Text box: 70% of column width, centred, not full row
        int tbW = juce::roundToInt (area.getWidth() * 0.70f);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, tbW, 16);
    };

    // Row 1: Freq, Gain, Q
    auto row1 = bounds.removeFromTop (rowHeight);
    int colW = row1.getWidth() / 3;
    
    layoutKnob (freqSlider, freqLabel, row1.removeFromLeft (colW));
    layoutKnob (gainSlider, gainLabel, row1.removeFromLeft (colW));
    layoutKnob (qSlider,    qLabel,    row1);

    // Row 2: Threshold, Ratio
    auto row2 = bounds.removeFromTop (rowHeight);
    colW = row2.getWidth() / 2;
    layoutKnob (thresholdSlider, threshLabel, row2.removeFromLeft (colW));
    layoutKnob (ratioSlider,     ratioLabel,  row2);

    // Row 3: Attack, Release
    // Use remaining height to avoid rounding errors or cutoffs
    auto row3 = bounds; 
    colW = row3.getWidth() / 2;
    layoutKnob (attackSlider,  attackLabel,  row3.removeFromLeft (colW));
    layoutKnob (releaseSlider, releaseLabel, row3);
}

//==============================================================================
// DynamicEQAudioProcessorEditor
//==============================================================================
DynamicEQAudioProcessorEditor::DynamicEQAudioProcessorEditor (DynamicEQAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      spectrumComponent (p)
{
    setLookAndFeel (&getDarkLookAndFeel());

    addAndMakeVisible (spectrumComponent);

    // Set up the scrollable control area: Viewport wraps a plain container Component.
    // Strips are children of the container, not of the editor directly.
    controlViewport.setViewedComponent (&controlContainer, false);
    controlViewport.setScrollBarsShown (false, false);   // built-in bars hidden; nav bar has custom bar
    addAndMakeVisible (controlViewport);

    // Nav bar custom horizontal scrollbar
    addAndMakeVisible (navScrollBar);
    navScrollBar.addListener (this);
    navScrollBar.setAutoHide (false);
    navScrollBar.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xFF5A9FD4));

    // Create ALL bands upfront (parameters are pre-registered for all numBands)
    // Visibility is controlled by activeBandCount + collapse state
    for (int i = 0; i < DynamicEQAudioProcessor::numBands; ++i)
    {
        auto* strip = new BandControlStrip (p, i);
        controlContainer.addChildComponent (strip);   // parented to container, not editor
        bandStrips.add (strip);
    }

    // Nav bar: +/- band buttons
    addAndMakeVisible (addBandBtn);
    addAndMakeVisible (removeBandBtn);
    addBandBtn.setTooltip    (juce::String::fromUTF8 ("\u6dfb\u52a0\u9891\u6bb5"));
    removeBandBtn.setTooltip (juce::String::fromUTF8 ("\u5220\u9664\u9891\u6bb5"));

    addBandBtn.onClick = [this]()
    {
        audioProcessor.setActiveBandCount (audioProcessor.getActiveBandCount() + 1);
        updateBandVisibility();
        resized();
    };
    removeBandBtn.onClick = [this]()
    {
        audioProcessor.setActiveBandCount (audioProcessor.getActiveBandCount() - 1);
        updateBandVisibility();
        resized();
    };

    // Nav bar: collapse/expand button
    addAndMakeVisible (collapseBtn);
    collapseBtn.setButtonText (juce::String::fromUTF8 ("\u25bc"));  // ▼
    collapseBtn.setTooltip    (juce::String::fromUTF8 ("\u6536\u8d77/\u5c55\u5f00\u63a7\u4ef6\u533a"));
    collapseBtn.onClick = [this]()
    {
        controlAreaCollapsed = !controlAreaCollapsed;
        collapseBtn.setButtonText (controlAreaCollapsed
            ? juce::String::fromUTF8 ("\u25b2")   // ▲
            : juce::String::fromUTF8 ("\u25bc")); // ▼
        updateBandVisibility();
        resized();
    };

    updateBandVisibility();

    setSize (960, 660);
    setResizable (true, true);
    setResizeLimits (800, 480, 1920, 1080);
}

DynamicEQAudioProcessorEditor::~DynamicEQAudioProcessorEditor()
{
    navScrollBar.removeListener (this);
    setLookAndFeel (nullptr);
}

void DynamicEQAudioProcessorEditor::updateBandVisibility()
{
    int active = audioProcessor.getActiveBandCount();
    for (int i = 0; i < bandStrips.size(); ++i)
        bandStrips[i]->setVisible (i < active && !controlAreaCollapsed);
    addBandBtn.setEnabled    (active < DynamicEQAudioProcessor::numBands);
    removeBandBtn.setEnabled (active > 1);
    repaint();   // ensure "频段 x/8" label re-draws in paint()
}

void DynamicEQAudioProcessorEditor::scrollBarMoved (juce::ScrollBar* /*bar*/, double newRangeStart)
{
    // Drive the viewport position from the nav bar scrollbar
    controlViewport.setViewPosition (static_cast<int> (newRangeStart), 0);
}

void DynamicEQAudioProcessorEditor::updateNavScrollBar()
{
    // Total content width vs. visible width
    double totalW  = static_cast<double> (controlContainer.getWidth());
    double visibleW = static_cast<double> (controlViewport.getMaximumVisibleWidth());
    bool hasScroll = totalW > visibleW + 2.0;   // 2px tolerance
    navScrollBar.setVisible (hasScroll && !controlAreaCollapsed);
    if (hasScroll)
    {
        navScrollBar.setRangeLimits (0.0, totalW);
        navScrollBar.setCurrentRange (static_cast<double>(controlViewport.getViewPositionX()), visibleW);
    }
}

void DynamicEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF0F0F1E));

    // Title bar
    g.setColour (juce::Colour (0xCCFFFFFF));
    g.setFont (juce::FontOptions (18.0f).withStyle ("Bold"));
    g.drawText ("Dynamic EQ", getLocalBounds().removeFromTop (36), juce::Justification::centred);

    // Nav bar background
    if (navBarBounds.getHeight() > 0)
    {
        auto nb = navBarBounds.toFloat();
        g.setColour (juce::Colour (0xFF14142A));
        g.fillRect (nb);
        // Top separator
        g.setColour (juce::Colour (0xFF2D2D55));
        g.drawHorizontalLine (static_cast<int> (nb.getY()),      nb.getX(), nb.getRight());
        // Bottom separator
        g.drawHorizontalLine (static_cast<int> (nb.getBottom()), nb.getX(), nb.getRight());

        // Active band count label on the left
        int active = audioProcessor.getActiveBandCount();
        g.setColour (juce::Colour (0xFF6677AA));
        g.setFont (juce::FontOptions (11.5f));
        juce::String infoText = juce::String::fromUTF8 ("\u9891\u6bb5: ")
                                + juce::String (active)
                                + " / "
                                + juce::String (DynamicEQAudioProcessor::numBands);
        g.drawText (infoText, nb.reduced (10.0f, 0.0f).toNearestInt(),
                    juce::Justification::centredLeft);
    }
}

void DynamicEQAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // ---- Title bar (top) ----
    bounds.removeFromTop (36);

    // ---- Control area (bottom, conditional) ----
    if (!controlAreaCollapsed)
    {
        auto viewportArea = bounds.removeFromBottom (controlH).reduced (8, 0);
        controlViewport.setVisible (true);
        controlViewport.setBounds (viewportArea);

        int active  = audioProcessor.getActiveBandCount();
        // How many pixels each strip naturally gets if we fill the visible width
        int areaW   = controlViewport.getMaximumVisibleWidth();
        int natural = (active > 0) ? areaW / active : areaW;
        // Clamp to [stripMinW, stripMaxW]
        int stripW  = juce::jlimit (stripMinW, stripMaxW, natural);
        int totalW  = stripW * active;
        // Container must be at least as wide as the viewport (no gap on right)
        int contW   = juce::jmax (totalW, areaW);
        int contH   = controlViewport.getMaximumVisibleHeight();
        controlContainer.setBounds (0, 0, contW, contH);

        for (int i = 0; i < bandStrips.size(); ++i)
        {
            if (i < active)
                bandStrips[i]->setBounds (i * stripW + 3, 4, stripW - 6, contH - 8);
        }
    }
    else
    {
        controlViewport.setVisible (false);
    }

    // ---- Nav bar (sits immediately below spectrum / above control area) ----
    navBarBounds = bounds.removeFromBottom (navBarH);
    {
        auto btnArea = navBarBounds.reduced (4, 4);

        // Right side: collapse ▼/▲ (28px), gap, + (26px), gap, - (26px)
        collapseBtn.setBounds    (btnArea.removeFromRight (28));
        btnArea.removeFromRight  (4);
        addBandBtn.setBounds     (btnArea.removeFromRight (26));
        btnArea.removeFromRight  (4);
        removeBandBtn.setBounds  (btnArea.removeFromRight (26));
        btnArea.removeFromRight  (4);

        // Left side: label occupies ~90px, scrollbar takes whatever remains
        auto labelArea  = btnArea.removeFromLeft (90);
        (void) labelArea;  // drawn in paint() via navBarBounds, no widget needed here
        // The remaining btnArea is the scrollbar slot
        navScrollBar.setBounds (btnArea.reduced (0, 5));
    }

    // Update scrollbar range now that controlContainer size is known
    updateNavScrollBar();

    // ---- Spectrum (everything that remains) ----
    spectrumComponent.setBounds (bounds.reduced (8, 4));
}
