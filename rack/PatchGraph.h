#pragma once
#include "Module.h"
#include <memory>
#include <mutex>

namespace rack {

// A cable joins two jacks. It is undirected: the graph works out which end is the output.
// Output->input carries signal. Output->output and input->input do nothing.
// Several cables on one output: the signal goes to all of them. Several on one input: they are summed.
struct Cable { int modA, jackA, modB, jackB; };

// Runs several modules and the cables between them, in one graph.
// Timing law: forward cables are sample-accurate. A cable that closes a feedback loop (including a module patched
// to itself) is delayed by exactly one sample. Only the newest cable in each loop is delayed: setCables() takes the
// cables oldest first, and a cable is delayed only if the older, undelayed cables already lead from its input module
// back to its output module. Modules run in the order those undelayed cables imply, so the order of addModule() calls
// does not change timing.
class PatchGraph {
public:
    static constexpr int kFeedbackDelay = 1;   // samples, on the newest cable of each feedback loop

    int  addModule(Module* m);             // not owned; call before prepare()
    void prepare(double sampleRate, int maxBlock);
    void setCables(const std::vector<Cable>& cablesOldestFirst);   // any thread except audio; picked up at the next process()
    // Signal used for an input jack while nothing is patched into it (e.g. host audio normalled to a mixer input).
    // Set by the host of the graph, never by a module. Pointer must stay valid for the next process() call.
    void setNormal(int mod, int jack, const float* samples);
    void process(int numSamples);          // audio thread, numSamples <= maxBlock
    const float* output(int mod, int jack) const;      // valid after process()
    bool isConnected(int mod, int jack) const;          // audio thread view

    int moduleCount() const { return (int) mods.size(); }
    Module* module(int i) const { return mods[(size_t) i]; }

private:
    struct Src { int mod, jack; bool delayed; };
    struct Routing {
        std::vector<std::vector<std::vector<Src>>> in;   // [mod][jack] -> sources
        std::vector<int> order;                          // module run order
        std::vector<Src> delayedSrcs;                    // outputs read through a 1-sample delay
    };
    std::vector<Module*> mods;
    std::vector<std::vector<std::vector<float>>> inBuf, outBuf;           // [mod][jack][sample]
    std::vector<std::vector<float>> prev;                                 // [mod][jack] last output sample
    std::vector<std::vector<const float*>> normals;
    std::vector<std::vector<const float*>> inPtr;
    std::vector<std::vector<float*>> outPtr;
    std::vector<float> zeros;
    Routing active;
    std::unique_ptr<Routing> pending;
    std::mutex pendingLock;
    bool hasPending = false;
    int maxBlock = 0;
    Routing makeRouting(const std::vector<Cable>& cables) const;
};

} // namespace rack
