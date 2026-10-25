#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Presets change the sound only. They never touch Key, Scale, Range or Tuning, which belong to your song.
    struct Preset
    {
        const char* name;
        bool correct;
        float retuneMs, amountPct;
        float hpf, drive, body, presence, air, comp, out;
    };

    const Preset presets[] =
    {
        //  name                        corr  retune amount  hpf   drive body  pres  air   comp  out
        { "Natural Tune",               true,   90.f,  85.f,  80.f, 0.15f, 1.f, 1.5f, 2.f, 0.30f,  0.f },
        { "Hard Tune (robot pop)",      true,    0.f, 100.f,  90.f, 0.25f, 0.f, 2.0f, 2.f, 0.40f, -1.f },
        { "Clean Vocal Preamp",         false,  20.f, 100.f,  80.f, 0.10f, 0.f, 2.0f, 3.f, 0.35f,  0.f },
        { "Warm Radio",                 false,  20.f, 100.f, 110.f, 0.45f, 3.f, 3.0f, 0.f, 0.60f, -2.f },
        { "Subtle Polish",              true,  150.f,  60.f,  80.f, 0.10f, 0.f, 1.0f, 2.f, 0.25f,  0.f },
    };
    constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));

    using APF = juce::AudioParameterFloat;
    using APC = juce::AudioParameterChoice;
    using APB = juce::AudioParameterBool;

    // lowText: shown instead of the number when the value is at or below lowThreshold (e.g. "Off", "Instant").
    std::unique_ptr<APF> makeFloat (const char* id, const char* name, float lo, float hi, float def,
                                    const char* unit, int decimals, float centre = 0.0f,
                                    const char* lowText = nullptr, float lowThreshold = 0.0f)
    {
        juce::NormalisableRange<float> range (lo, hi);
        if (centre > 0.0f) range.setSkewForCentre (centre);

        const juce::String suffix = juce::String (" ") + unit;
        const juce::String low = lowText != nullptr ? juce::String (lowText) : juce::String();
        auto attrs = juce::AudioParameterFloatAttributes()
                         .withStringFromValueFunction ([decimals, suffix, low, lowThreshold] (float v, int)
                                                       {
                                                           if (low.isNotEmpty() && v <= lowThreshold) return low;
                                                           return juce::String (v, decimals) + suffix;
                                                       });

        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def, attrs);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout VocalProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<APB> (juce::ParameterID { "correct", 1 }, "Pitch Correction", true));
    layout.add (std::make_unique<APC> (juce::ParameterID { "key", 1 }, "Key",
                                       juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    layout.add (std::make_unique<APC> (juce::ParameterID { "scale", 1 }, "Scale",
                                       juce::StringArray { "Chromatic", "Major", "Minor", "Maj Pent", "Min Pent", "Dorian" }, 1));
    layout.add (std::make_unique<APC> (juce::ParameterID { "range", 1 }, "Voice Range",
                                       juce::StringArray { "Low", "Normal", "High" }, 1));
    layout.add (makeFloat ("retune", "Retune Speed", 0.0f, 400.0f, 20.0f, "ms", 0, 60.0f, "Instant", 0.5f));
    layout.add (makeFloat ("amount", "Amount", 0.0f, 100.0f, 100.0f, "%", 0));
    layout.add (makeFloat ("tuning", "Tuning (A4)", 430.0f, 450.0f, 440.0f, "Hz", 1));

    layout.add (makeFloat ("inGain",   "Input Gain", -24.0f, 24.0f, 0.0f, "dB", 1));
    layout.add (makeFloat ("hpf",      "High-Pass",  20.0f, 300.0f, 80.0f, "Hz", 0, 90.0f, "Off", 21.0f));
    layout.add (makeFloat ("drive",    "Drive",      0.0f, 1.0f, 0.2f, "", 2));
    layout.add (makeFloat ("body",     "Body",      -12.0f, 12.0f, 0.0f, "dB", 1));
    layout.add (makeFloat ("presence", "Presence",  -12.0f, 12.0f, 0.0f, "dB", 1));
    layout.add (makeFloat ("air",      "Air",       -12.0f, 12.0f, 0.0f, "dB", 1));
    layout.add (makeFloat ("comp",     "Compressor", 0.0f, 1.0f, 0.0f, "", 2, 0.0f, "Off", 0.001f));
    layout.add (makeFloat ("outGain",  "Output",    -24.0f, 12.0f, 0.0f, "dB", 1));

    return layout;
}

//==============================================================================
VocalProcessor::VocalProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    pCorrect = raw ("correct"); pKey = raw ("key");       pScale = raw ("scale");   pRange = raw ("range");
    pRetune  = raw ("retune");  pAmount = raw ("amount"); pTuning = raw ("tuning");
    pInGain  = raw ("inGain");  pHpf = raw ("hpf");       pDrive = raw ("drive");   pBody = raw ("body");
    pPresence = raw ("presence"); pAir = raw ("air");     pComp = raw ("comp");     pOutGain = raw ("outGain");
}

bool VocalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void VocalProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    updateEngineParams();
    setLatencySamples (engine.latencySamples());
}

void VocalProcessor::updateEngineParams()
{
    vocal::Params p;
    p.correct    = pCorrect->load() > 0.5f;
    p.key        = (int) pKey->load();
    p.scale      = (int) pScale->load();
    p.range      = (int) pRange->load();
    p.retuneMs   = pRetune->load();
    p.amount     = pAmount->load() * 0.01f;
    p.tuningHz   = pTuning->load();
    p.inGainDb   = pInGain->load();
    p.hpfHz      = pHpf->load();
    p.drive      = pDrive->load();
    p.bodyDb     = pBody->load();
    p.presenceDb = pPresence->load();
    p.airDb      = pAir->load();
    p.comp       = pComp->load();
    p.outGainDb  = pOutGain->load();
    engine.setParams (p);
}

void VocalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, numSamples);

    updateEngineParams();
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (numIn, numOut), numSamples);

    detectedMidi.store (engine.detectedMidi());
    targetMidi.store (engine.targetMidi());
}

//==============================================================================
int VocalProcessor::getNumPrograms() { return numPresets; }

const juce::String VocalProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void VocalProcessor::setParameterValue (const char* id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

void VocalProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, numPresets)) return;
    currentProgram = index;
    const Preset& p = presets[index];

    setParameterValue ("correct", p.correct ? 1.0f : 0.0f);
    setParameterValue ("retune", p.retuneMs);
    setParameterValue ("amount", p.amountPct);
    setParameterValue ("hpf", p.hpf);
    setParameterValue ("drive", p.drive);
    setParameterValue ("body", p.body);
    setParameterValue ("presence", p.presence);
    setParameterValue ("air", p.air);
    setParameterValue ("comp", p.comp);
    setParameterValue ("outGain", p.out);
}

//==============================================================================
void VocalProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VocalProcessor::createEditor() { return new VocalEditor (*this); }

// This creates the plugin instance. The host calls it.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VocalProcessor(); }
