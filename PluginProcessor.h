#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "VocalEngine.h"

class VocalProcessor : public juce::AudioProcessor
{
public:
    VocalProcessor();
    ~VocalProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vocal Tune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Shown in the editor's note display (MIDI note numbers, -1 = no pitch detected).
    std::atomic<float> detectedMidi { -1.0f };
    std::atomic<float> targetMidi { -1.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateEngineParams();
    void setParameterValue (const char* id, float plainValue);

    vocal::Engine engine;
    int currentProgram = 0;

    std::atomic<float> *pCorrect, *pKey, *pScale, *pRange, *pRetune, *pAmount, *pTuning,
                       *pInGain, *pHpf, *pDrive, *pBody, *pPresence, *pAir, *pComp, *pOutGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalProcessor)
};
