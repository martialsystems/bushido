#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../engine/Sq10Module.h"
#include "../rack/PatchGraph.h"
#include "../ui/CableLayer.h"

class Sq10Processor : public juce::AudioProcessor {
public:
    Sq10Processor();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SQ-10 Sequencer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // used by the editor
    static juce::String paramIdFor(const std::string& moduleParamId);   // "CH:PORTA A" -> "CH_PORTA_A"
    juce::RangedAudioParameter* parameter(const juce::String& layoutId) const;
    void pressButton(const juce::String& layoutId, bool down);
    float indicator(const juce::String& layoutId) const;
    void setCables(const std::vector<CableSpec>& cables);               // message thread
    std::vector<CableSpec> getCables() const { return cables; }
    std::function<void()> onStateLoaded;                                 // editor reloads its cables

    Sq10Module sq;                               // must come before apvts: the parameter layout is built from it
    rack::PatchGraph graph;
    juce::AudioProcessorValueTreeState apvts;

private:
    int sqIndex = 0;
    std::vector<std::atomic<float>*> raw;       // per module param, nullptr for buttons
    std::vector<float> lastSent;
    std::vector<CableSpec> cables;
    std::vector<float> hostL, hostR;
    int maxBlock = 512;
    int midiNote[2] = { -1, -1 }; bool gatePrev[2] = { false, false };
    void applyCables();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(const Sq10Module&);
};
