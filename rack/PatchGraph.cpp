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
    inBuf.assign(n, {}); outBuf.assign(n, {}); hist.assign(n, {}); histLen.assign(n, {});
    normals.assign(n, {}); inPtr.assign(n, {}); outPtr.assign(n, {}); active.in.assign(n, {});
    for (size_t m = 0; m < n; ++m) {
        const size_t nj = mods[m]->jacks().size();
        inBuf[m].assign(nj, std::vector<float>((size_t) maxBlock, 0.0f));
        outBuf[m].assign(nj, std::vector<float>((size_t) maxBlock, 0.0f));
        hist[m].assign(nj, std::vector<float>((size_t) kSubBlock, 0.0f));
        histLen[m].assign(nj, kSubBlock);
        normals[m].assign(nj, nullptr);
        inPtr[m].assign(nj, nullptr); outPtr[m].assign(nj, nullptr);
        active.in[m].assign(nj, {});
        mods[m]->prepare(sampleRate, kSubBlock);
    }
}

void PatchGraph::setCables(const std::vector<Cable>& cables)
{
    auto r = std::make_unique<Routing>();
    r->in.resize(mods.size());
    for (size_t m = 0; m < mods.size(); ++m) r->in[m].resize(mods[m]->jacks().size());
    auto valid = [&](int m, int j) { return m >= 0 && m < (int) mods.size() && j >= 0 && j < (int) mods[(size_t) m]->jacks().size(); };
    for (const auto& c : cables) {
        if (! valid(c.modA, c.jackA) || ! valid(c.modB, c.jackB)) continue;
        const Dir da = mods[(size_t) c.modA]->jacks()[(size_t) c.jackA].dir;
        const Dir db = mods[(size_t) c.modB]->jacks()[(size_t) c.jackB].dir;
        if (da == Dir::Out && db == Dir::In) r->in[(size_t) c.modB][(size_t) c.jackB].push_back({ c.modA, c.jackA });
        else if (da == Dir::In && db == Dir::Out) r->in[(size_t) c.modA][(size_t) c.jackA].push_back({ c.modB, c.jackB });
    }
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
    const size_t nm = mods.size();
    for (int o = 0; o < numSamples; o += kSubBlock) {
        const int len = std::min(kSubBlock, numSamples - o);
        for (size_t m = 0; m < nm; ++m) {
            const auto& js = mods[m]->jacks();
            for (size_t j = 0; j < js.size(); ++j) {
                outPtr[m][j] = outBuf[m][j].data() + o;
                if (js[j].dir == Dir::Out) { inPtr[m][j] = zeros.data(); continue; }
                const auto& srcs = active.in[m][j];
                float* dst = inBuf[m][j].data() + o;
                if (srcs.empty()) {
                    const float* nrm = normals[m][j];
                    if (nrm) std::copy(nrm + o, nrm + o + len, dst); else std::fill(dst, dst + len, 0.0f);
                } else {
                    std::fill(dst, dst + len, 0.0f);
                    for (const auto& s : srcs) {
                        if ((size_t) s.mod < m) {                       // already ran this sub-block: same-time signal
                            const float* src = outBuf[(size_t) s.mod][(size_t) s.jack].data() + o;
                            for (int i = 0; i < len; ++i) dst[i] += src[i];
                        } else {                                        // runs later (or is this module): previous sub-block
                            const auto& h = hist[(size_t) s.mod][(size_t) s.jack]; const int hl = histLen[(size_t) s.mod][(size_t) s.jack];
                            for (int i = 0; i < len; ++i) dst[i] += h[(size_t) std::min(i, hl - 1)];
                        }
                    }
                }
                inPtr[m][j] = dst;
            }
            mods[m]->process(inPtr[m].data(), outPtr[m].data(), len);
        }
        for (size_t m = 0; m < nm; ++m)
            for (size_t j = 0; j < hist[m].size(); ++j) {
                std::copy(outBuf[m][j].data() + o, outBuf[m][j].data() + o + len, hist[m][j].data());
                histLen[m][j] = len;
            }
    }
}

} // namespace rack
