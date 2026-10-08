#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../engine/BushidoModule.h"
#include "../engine/BushidoState.h"
#include "../engine/MidiOut.h"
#include "../rack/PatchGraph.h"
#include "../ui/CableLayer.h"

class BushidoProcessor : public juce::AudioProcessor {
public:
    BushidoProcessor();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "BUSHIDO"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    // Patterns live in two banks, A and B, of up to 999 each. Bank A starts with the factory bank (assets/bushido_patterns.json, the INIT pattern);
    // SAVE appends the panel as the next number of a bank. Saved patterns go to a user file shared by every instance.
    // The host sees bank A then bank B as one program list.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
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
    struct Pattern { juce::String name; int format = 0; std::vector<std::pair<juce::String, float>> params; std::vector<CableSpec> cables; };
    static constexpr int kBankSize = 999;
    juce::StringArray patternNames(int bank) const;
    int loadedBank() const { return curBank.load(); }
    int loadedPattern() const { return curPattern.load(); }
    void loadPattern(int bank, int index);
    int savePattern(int bank, const juce::String& name);                // returns the new index, or -1 when the bank is full
    static juce::File userPatternFile();
    bool isBypassed() const { return bypass != nullptr && bypass->get(); }
    // The last load's migration report (SETUP tab): empty when the state was already format 1.
    std::vector<juce::String> migrationLines() const { const juce::ScopedLock sl(bankLock); return migration; }
    bool lawMismatch(int row) const { return row == 0 || row == 1 ? mismatch[row].load() : false; }
    bool isReadOnly() const { return readOnly.load(); }

    BushidoModule sq;                               // must come before apvts: the parameter layout is built from it
    rack::PatchGraph graph;
    juce::AudioProcessorValueTreeState apvts;

private:
    int sqIndex = 0;
    std::vector<std::atomic<float>*> raw;       // per module param, nullptr for buttons
    std::vector<float> lastSent;
    std::vector<CableSpec> cables;
    std::vector<float> hostL, hostR;
    int maxBlock = 512;
    BushidoMidiOut midiOut;                      // MIDI tab: channel, velocity, note from the target volts under each row's PITCH LAW
    BushidoModule::GateEvent events[BushidoModule::kMaxEvents];
    bool wasBypassed = false;
    juce::MemoryBlock readOnlyState;              // a state from a newer format: handed back unchanged while it is loaded
    juce::AudioParameterBool* bypass = nullptr;  // TOP:BYPASS, the rocker at the top left
    std::vector<Pattern> banks[2];
    int factoryCount = 0;                        // bank A's first entries; never written to the user file
    std::atomic<int> curBank { 0 }, curPattern { 0 };
    juce::CriticalSection bankLock;
    std::vector<juce::String> migration;         // guarded by bankLock
    std::atomic<bool> mismatch[2] { { false }, { false } }, readOnly { false };
    // Format 0 (or no format) -> 1 for a parameter map keyed by module param id; cables are global jack ids.
    void migrateParams(int fromFormat, std::map<std::string, float>& params, const std::vector<CableSpec>& cables);
    static Pattern patternFromVar(const juce::var&);
    static juce::var patternToVar(const Pattern&);
    void writeUserFile() const;
    void applyCables();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(const BushidoModule&);
};
