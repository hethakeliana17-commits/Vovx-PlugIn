#include "PluginEditor.h"

namespace
{
    const juce::Colour kBackground (0xff14161b);
    const juce::Colour kPanel      (0xff1e2128);
    const juce::Colour kAccent     (0xff3ddc97);
    const juce::Colour kWarm       (0xffff8a1f);
    const juce::Colour kText       (0xffe6e8ee);
    const juce::Colour kDimText    (0xff9aa0ad);

    juce::String noteName (int midi)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int pc = ((midi % 12) + 12) % 12;
        const int octave = (int) std::floor ((double) midi / 12.0) - 1;
        return juce::String (names[pc]) + juce::String (octave);
    }
}

//==============================================================================
NoteDisplay::NoteDisplay (VocalProcessor& p) : processor (p)
{
    startTimerHz (30);
}

void NoteDisplay::timerCallback()
{
    const float d = processor.detectedMidi.load();
    const float t = processor.targetMidi.load();
    const float cents = (d >= 0.0f && t >= 0.0f) ? juce::jlimit (-100.0f, 100.0f, (d - t) * 100.0f) : 0.0f;

    if (d != shownDetected || t != shownTarget || std::fabs (cents - shownCents) > 0.5f)
    {
        shownDetected = d; shownTarget = t;
        shownCents += (cents - shownCents) * 0.6f;
        repaint();
    }
}

void NoteDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (r, 8.0f);

    auto left = getLocalBounds().reduced (16, 8).removeFromLeft (190);
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (34.0f, juce::Font::bold)));
    const bool hasPitch = shownDetected >= 0.0f;
    g.drawText (hasPitch ? noteName ((int) std::lround (shownDetected)) : juce::String ("--"),
                left.removeFromTop (44), juce::Justification::centredLeft);

    g.setColour (kDimText);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (hasPitch ? "corrected to " + noteName ((int) std::lround (shownTarget)) : juce::String ("listening..."),
                left, juce::Justification::topLeft);

    // Cents meter: centre = on the target note, edges = 100 cents flat / sharp.
    auto meter = getLocalBounds().reduced (20, 0).withTrimmedLeft (200).toFloat();
    const float cy = meter.getCentreY();
    g.setColour (juce::Colour (0xff2c313b));
    g.fillRoundedRectangle (meter.getX(), cy - 3.0f, meter.getWidth(), 6.0f, 3.0f);
    for (int c = -100; c <= 100; c += 50)
    {
        const float x = meter.getCentreX() + (float) c / 100.0f * meter.getWidth() * 0.5f;
        g.setColour (c == 0 ? kAccent : kDimText);
        g.fillRect (x - 1.0f, cy - (c == 0 ? 14.0f : 9.0f), 2.0f, c == 0 ? 28.0f : 18.0f);
    }
    g.setColour (kDimText);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("flat", juce::Rectangle<float> (meter.getX(), cy + 16.0f, 40.0f, 14.0f), juce::Justification::left);
    g.drawText ("sharp", juce::Rectangle<float> (meter.getRight() - 40.0f, cy + 16.0f, 40.0f, 14.0f), juce::Justification::right);

    if (hasPitch)
    {
        const float x = meter.getCentreX() + shownCents / 100.0f * meter.getWidth() * 0.5f;
        g.setColour (std::fabs (shownCents) < 15.0f ? kAccent : kWarm);
        g.fillEllipse (x - 8.0f, cy - 8.0f, 16.0f, 16.0f);
    }
}

//==============================================================================
ParamControl::ParamControl (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                            const juce::String& text, Kind controlKind)
    : kind (controlKind)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (12.0f)));
    label.setColour (juce::Label::textColourId, kDimText);
    addAndMakeVisible (label);

    switch (kind)
    {
        case Kind::Knob:
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
            slider.setColour (juce::Slider::thumbColourId, kText);
            slider.setColour (juce::Slider::textBoxTextColourId, kText);
            slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible (slider);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
            break;

        case Kind::Choice:
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
                combo.addItemList (p->choices, 1);
            addAndMakeVisible (combo);
            comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, combo);
            break;

        case Kind::Toggle:
            toggle.setButtonText ("On");
            toggle.setColour (juce::ToggleButton::textColourId, kText);
            toggle.setColour (juce::ToggleButton::tickColourId, kAccent);
            addAndMakeVisible (toggle);
            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, toggle);
            break;
    }
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));

    switch (kind)
    {
        case Kind::Knob:   slider.setBounds (r); break;
        case Kind::Choice: combo.setBounds (r.withSizeKeepingCentre (r.getWidth() - 6, 24)); break;
        case Kind::Toggle: toggle.setBounds (r.withSizeKeepingCentre (r.getWidth() - 6, 24)); break;
    }
}

//==============================================================================
VocalEditor::VocalEditor (VocalProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), noteDisplay (p)
{
    using K = ParamControl::Kind;

    auto& pitch = addSection ("PITCH CORRECTION");
    addControl (pitch, "correct", "Correct",     K::Toggle);
    addControl (pitch, "key",     "Key",         K::Choice);
    addControl (pitch, "scale",   "Scale",       K::Choice);
    addControl (pitch, "range",   "Voice",       K::Choice);
    addControl (pitch, "retune",  "Retune Speed", K::Knob);
    addControl (pitch, "amount",  "Amount",      K::Knob);
    addControl (pitch, "tuning",  "Tuning A4",   K::Knob);

    auto& pre = addSection ("VOCAL PREAMP");
    addControl (pre, "inGain",   "Input",    K::Knob);
    addControl (pre, "hpf",      "High-Pass", K::Knob);
    addControl (pre, "drive",    "Drive",    K::Knob);
    addControl (pre, "body",     "Body",     K::Knob);
    addControl (pre, "presence", "Presence", K::Knob);
    addControl (pre, "air",      "Air",      K::Knob);
    addControl (pre, "comp",     "Compress", K::Knob);
    addControl (pre, "outGain",  "Output",   K::Knob);

    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this] { processor.setCurrentProgram (presetBox.getSelectedItemIndex()); };
    addAndMakeVisible (presetBox);
    addAndMakeVisible (noteDisplay);

    setSize (680, 450);
}

VocalEditor::Section& VocalEditor::addSection (const juce::String& title)
{
    sections.push_back (std::make_unique<Section>());
    sections.back()->title = title;
    return *sections.back();
}

void VocalEditor::addControl (Section& s, const char* id, const char* text, ParamControl::Kind kind)
{
    auto c = std::make_unique<ParamControl> (processor.apvts, id, text, kind);
    addAndMakeVisible (*c);
    s.controls.push_back (std::move (c));
}

void VocalEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    g.setColour (kAccent);
    g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    g.drawText ("VOCAL TUNE", 20, 12, 300, 34, juce::Justification::centredLeft);

    for (auto& s : sections)
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (s->area.toFloat(), 8.0f);
        g.setColour (kDimText);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (s->title, s->area.getX() + 12, s->area.getY() + 6, s->area.getWidth() - 24, 16,
                    juce::Justification::centredLeft);
    }
}

void VocalEditor::resized()
{
    constexpr int colW = 80, pad = 10, gap = 8, rowH = 136, titleH = 24;

    auto bounds = getLocalBounds().reduced (10);
    auto header = bounds.removeFromTop (44);
    presetBox.setBounds (header.removeFromRight (200).reduced (0, 8));

    noteDisplay.setBounds (bounds.removeFromTop (84));
    bounds.removeFromTop (gap);

    int x = bounds.getX();
    int y = bounds.getY();

    for (auto& s : sections)
    {
        const int w = (int) s->controls.size() * colW + pad * 2;
        if (x + w > bounds.getRight() + 1)
        {
            x = bounds.getX();
            y += rowH + gap;
        }

        s->area = { x, y, w, rowH };

        auto inner = s->area.reduced (pad, 0);
        inner.removeFromTop (titleH);
        inner.removeFromBottom (6);
        for (auto& c : s->controls)
            c->setBounds (inner.removeFromLeft (colW));

        x += w + gap;
    }
}
