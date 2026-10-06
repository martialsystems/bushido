#pragma once
#include "Module.h"
#include <memory>
#include <mutex>

namespace rack {

// A cable joins two jacks. It is undirected: the graph works out which end is the output.
// Output->input carries signal. Output->output and input->input do nothing.
// Several cables on one output: the signal goes to all of them. Several on one input: they are summed.
struct Cable { int modA, jackA, modB, jackB; };

class PatchGraph {
public:
    static constexpr int kSubBlock = 16;   // a cable that feeds "backwards" in module order arrives <= 16 samples late

    int  addModule(Module* m);             // not owned; call before prepare()
    void prepare(double sampleRate, int maxBlock);
    void setCables(const std::vector<Cable>& cables);   // any thread except audio; picked up at the next process()
    // Signal used for an input jack while nothing is patched into it (e.g. host audio normalled to a mixer input).
    // Pointer must stay valid for the next process() call.
    void setNormal(int mod, int jack, const float* samples);
    void process(int numSamples);          // audio thread
    const float* output(int mod, int jack) const;      // valid after process()
    bool isConnected(int mod, int jack) const;          // audio thread view

    int moduleCount() const { return (int) mods.size(); }
    Module* module(int i) const { return mods[(size_t) i]; }

private:
    struct Src { int mod, jack; };
    struct Routing { std::vector<std::vector<std::vector<Src>>> in; };   // [mod][jack] -> sources
    std::vector<Module*> mods;
    std::vector<std::vector<std::vector<float>>> inBuf, outBuf, hist;    // [mod][jack][sample]
    std::vector<std::vector<int>> histLen;
    std::vector<std::vector<const float*>> normals;
    std::vector<std::vector<const float*>> inPtr;
    std::vector<std::vector<float*>> outPtr;
    std::vector<float> zeros;
    Routing active;
    std::unique_ptr<Routing> pending;
    std::mutex pendingLock;
    bool hasPending = false;
    int maxBlock = 0;
};

} // namespace rack
