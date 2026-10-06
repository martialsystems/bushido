#include "PatchGraph.h"
#include <algorithm>

namespace rack {

int findJack(const Module& m, const std::string& id)  { const auto& j = m.jacks();  for (size_t i = 0; i < j.size(); ++i) if (j[i].id == id) return (int) i; return -1; }
int findParam(const Module& m, const std::string& id) { const auto& p = m.params(); for (size_t i = 0; i < p.size(); ++i) if (p[i].id == id) return (int) i; return -1; }

int PatchGraph::addModule(Module* m) { mods.push_back(m); return (int) mods.size() - 1; }

void PatchGraph::prepare(double sampleRate, int maxBlockSize)
{
    maxBlock = maxBlockSize;
    zeros.assign((size_t) maxBlock, 0.0f);
    const size_t n = mods.size();
    inBuf.assign(n, {}); outBuf.assign(n, {}); prev.assign(n, {});
    normals.assign(n, {}); inPtr.assign(n, {}); outPtr.assign(n, {});
    for (size_t m = 0; m < n; ++m) {
        const size_t nj = mods[m]->jacks().size();
        inBuf[m].assign(nj, std::vector<float>((size_t) maxBlock, 0.0f));
        outBuf[m].assign(nj, std::vector<float>((size_t) maxBlock, 0.0f));
        prev[m].assign(nj, 0.0f);
        normals[m].assign(nj, nullptr);
        inPtr[m].assign(nj, nullptr); outPtr[m].assign(nj, nullptr);
        mods[m]->prepare(sampleRate, maxBlock);
    }
    active = makeRouting({});
}

PatchGraph::Routing PatchGraph::makeRouting(const std::vector<Cable>& cables) const
{
    const int nm = (int) mods.size();
    Routing r;
    r.in.resize((size_t) nm);
    for (int m = 0; m < nm; ++m) r.in[(size_t) m].resize(mods[(size_t) m]->jacks().size());
    std::vector<std::vector<int>> next((size_t) nm);           // undelayed module edges: from -> to
    auto reaches = [&](int from, int to) {
        std::vector<char> seen((size_t) nm, 0); std::vector<int> stack { from };
        while (! stack.empty()) {
            const int m = stack.back(); stack.pop_back();
            if (m == to) return true;
            if (seen[(size_t) m]) continue;
            seen[(size_t) m] = 1;
            for (int t : next[(size_t) m]) stack.push_back(t);
        }
        return false;
    };
    auto valid = [&](int m, int j) { return m >= 0 && m < nm && j >= 0 && j < (int) mods[(size_t) m]->jacks().size(); };
    for (const auto& c : cables) {                             // oldest first
        if (! valid(c.modA, c.jackA) || ! valid(c.modB, c.jackB)) continue;
        const Dir da = mods[(size_t) c.modA]->jacks()[(size_t) c.jackA].dir;
        const Dir db = mods[(size_t) c.modB]->jacks()[(size_t) c.jackB].dir;
        int om, oj, im, ij;
        if (da == Dir::Out && db == Dir::In)      { om = c.modA; oj = c.jackA; im = c.modB; ij = c.jackB; }
        else if (da == Dir::In && db == Dir::Out) { om = c.modB; oj = c.jackB; im = c.modA; ij = c.jackA; }
        else continue;
        const bool delayed = reaches(im, om);                  // this cable closes a loop: it is the newest one in it
        r.in[(size_t) im][(size_t) ij].push_back({ om, oj, delayed });
        if (delayed) r.delayedSrcs.push_back({ om, oj, true });
        else next[(size_t) om].push_back(im);
    }
    std::vector<int> indeg((size_t) nm, 0);                    // run order: topological over undelayed cables, lowest index first
    for (int m = 0; m < nm; ++m) for (int t : next[(size_t) m]) ++indeg[(size_t) t];
    std::vector<char> done((size_t) nm, 0);
    while ((int) r.order.size() < nm) {
        int pick = -1;
        for (int m = 0; m < nm && pick < 0; ++m) if (! done[(size_t) m] && indeg[(size_t) m] == 0) pick = m;
        done[(size_t) pick] = 1; r.order.push_back(pick);
        for (int t : next[(size_t) pick]) --indeg[(size_t) t];
    }
    return r;
}

void PatchGraph::setCables(const std::vector<Cable>& cables)
{
    auto r = std::make_unique<Routing>(makeRouting(cables));
    std::lock_guard<std::mutex> g(pendingLock);
    pending = std::move(r);                       // the previous pending (old routing or one never picked up) is freed here, off the audio thread
    hasPending = true;
}

void PatchGraph::setNormal(int mod, int jack, const float* s) { normals[(size_t) mod][(size_t) jack] = s; }

bool PatchGraph::isConnected(int mod, int jack) const { return ! active.in[(size_t) mod][(size_t) jack].empty(); }

const float* PatchGraph::output(int mod, int jack) const { return outBuf[(size_t) mod][(size_t) jack].data(); }

void PatchGraph::process(int numSamples)
{
    if (pendingLock.try_lock()) {                 // never blocks the audio thread
        if (hasPending && pending && pending->in.size() == active.in.size()) { std::swap(active, *pending); hasPending = false; }
        pendingLock.unlock();
    }
    // With a feedback cable the graph runs one sample at a time, so that cable is exactly one sample late.
    // Without one, every cable is forward and the whole block runs at once, sample-accurate.
    const int sub = active.delayedSrcs.empty() ? std::max(1, numSamples) : kFeedbackDelay;
    for (int o = 0; o < numSamples; o += sub) {
        const int len = std::min(sub, numSamples - o);
        for (int m : active.order) {
            const auto& js = mods[(size_t) m]->jacks();
            for (size_t j = 0; j < js.size(); ++j) {
                outPtr[(size_t) m][j] = outBuf[(size_t) m][j].data() + o;
                if (js[j].dir == Dir::Out) { inPtr[(size_t) m][j] = zeros.data(); continue; }
                const auto& srcs = active.in[(size_t) m][j];
                float* dst = inBuf[(size_t) m][j].data() + o;
                if (srcs.empty()) {
                    const float* nrm = normals[(size_t) m][j];
                    if (nrm) std::copy(nrm + o, nrm + o + len, dst); else std::fill(dst, dst + len, 0.0f);
                } else {
                    std::fill(dst, dst + len, 0.0f);
                    for (const auto& s : srcs) {
                        if (s.delayed) { dst[0] += prev[(size_t) s.mod][(size_t) s.jack]; continue; }   // len == 1 here
                        const float* src = outBuf[(size_t) s.mod][(size_t) s.jack].data() + o;        // already ran: same sample
                        for (int i = 0; i < len; ++i) dst[i] += src[i];
                    }
                }
                inPtr[(size_t) m][j] = dst;
            }
            mods[(size_t) m]->process(inPtr[(size_t) m].data(), outPtr[(size_t) m].data(), len);
        }
        for (const auto& s : active.delayedSrcs) prev[(size_t) s.mod][(size_t) s.jack] = outBuf[(size_t) s.mod][(size_t) s.jack][(size_t) (o + len - 1)];
    }
    if (numSamples > 0)                           // keep every output's last sample, so a loop patched later starts from it
        for (size_t m = 0; m < mods.size(); ++m)
            for (size_t j = 0; j < prev[m].size(); ++j) prev[m][j] = outBuf[m][j][(size_t) numSamples - 1];
}

} // namespace rack
