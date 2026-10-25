#pragma once

#include "PluginProcessor.h"

// Shows the detected note, the note it is being corrected to, and how far off the singer is.
class NoteDisplay : public juce::Component, private juce::Timer
{
public:
    explicit NoteDisplay (VocalProcessor& p);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    VocalProcessor& processor;
    float shownDetected = -1.0f, shownTarget = -1.0f, shownCents = 0.0f;
};

// One labelled control (knob, drop-down or switch) tied to a plugin parameter.
class ParamControl : public juce::Component
{
public:
    enum class Kind { Knob, Choice, Toggle };

    ParamControl (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                  const juce::String& text, Kind controlKind);

    void resized() override;

private:
    Kind kind;
    juce::Label label;
    juce::Slider slider;
    juce::ComboBox combo;
    juce::ToggleButton toggle;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   buttonAttachment;
};

class VocalEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocalEditor (VocalProcessor&);
    ~VocalEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::String title;
        std::vector<std::unique_ptr<ParamControl>> controls;
        juce::Rectangle<int> area;
    };

    Section& addSection (const juce::String& title);
    void addControl (Section& s, const char* id, const char* text, ParamControl::Kind kind);

    VocalProcessor& processor;
    std::vector<std::unique_ptr<Section>> sections;
    juce::ComboBox presetBox;
    NoteDisplay noteDisplay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalEditor)
};
