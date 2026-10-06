#pragma once
// Shared rack contract. Framework-free C++17. Every rack (BUSHIDO here, any other rack later) implements rack::Module,
// so one PatchGraph can run several racks and cables can join jacks on different racks.
// Signals are floats in volts (audio about +-5 V, gates 0/5 V).
#include <string>
#include <vector>

namespace rack {

enum class Dir { In, Out };

struct JackInfo  { std::string id; Dir dir; };            // id = "SECTION:LABEL", matches the panel layout file
struct ParamInfo { std::string id; float def; int positions; };
// positions: 0 = continuous knob (0..1), N >= 2 = N-position switch (0..1 in N steps), -1 = momentary button

struct IndicatorInfo { std::string id; };                  // LEDs and meters, value 0..1, read by the UI

class Module {
public:
    virtual ~Module() = default;
    virtual const char* name() const = 0;                  // rack name, e.g. "SQ-10" (BUSHIDO's rack id, kept for saved patches); global jack id = name + "/" + jack id
    virtual const std::vector<JackInfo>& jacks() const = 0;
    virtual const std::vector<ParamInfo>& params() const = 0;
    virtual const std::vector<IndicatorInfo>& indicators() const { static const std::vector<IndicatorInfo> none; return none; }

    virtual void prepare(double sampleRate, int maxBlock) = 0;
    // One pointer per jack, indexed like jacks(). in[i] is valid for every jack (zeros for outputs);
    // out[i] is valid for every jack (scratch for inputs). Called on the audio thread.
    virtual void process(const float* const* in, float* const* out, int numSamples) = 0;

    // Thread-safe: the UI thread may call setParam while the audio thread runs process().
    virtual void setParam(int index, float value) = 0;
    virtual float getParam(int index) const = 0;
    virtual float indicator(int index) const { (void) index; return 0.0f; }
};

int findJack(const Module& m, const std::string& id);     // -1 if missing
int findParam(const Module& m, const std::string& id);

} // namespace rack
