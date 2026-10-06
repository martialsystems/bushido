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
    // The 10 factory patterns (assets/sq10_patterns.json) are the host's programs and the PATTERN screen's list.
    int getNumPrograms() override { return juce::jmax(1, (int) patterns.size()); }
    int getCurrentProgram() override { return currentPattern.load(); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int i) override { return juce::isPositiveAndBelow(i, (int) patterns.size()) ? patterns[(size_t) i].name : juce::String(); }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass; }
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
    struct Pattern { juce::String name; std::vector<std::pair<juce::String, float>> params; std::vector<CableSpec> cables; };
    const std::vector<Pattern>& getPatterns() const { return patterns; }
    bool isBypassed() const { return bypass != nullptr && bypass->get(); }

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
    juce::AudioParameterBool* bypass = nullptr;  // TOP:BYPASS, the rocker at the top left
    std::vector<Pattern> patterns;
    std::atomic<int> currentPattern { 0 };
    void applyCables();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(const Sq10Module&);
};
