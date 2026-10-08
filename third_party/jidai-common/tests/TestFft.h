// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
#pragma once
// Test-only helpers: radix-2 FFT power spectrum and the line-spectrum alias measure used by the SHOGUN verify
// scripts (alias energy = everything in 0..20 kHz that is not on a multiple of the legitimate line spacing).
#include <cmath>
#include <complex>
#include <vector>

namespace jidai::test {

inline std::vector<double> powerSpectrum (const std::vector<double>& x)
{
    const size_t n = x.size();
    std::vector<std::complex<double>> a (x.begin(), x.end());
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    std::vector<double> p (n / 2 + 1);
    for (size_t k = 0; k <= n / 2; ++k) p[k] = std::norm (a[k]);
    return p;
}

// dB of (energy off the legitimate lines) / (energy on them), bins 1 .. 20 kHz.
inline double aliasDb (const std::vector<double>& y, double fs, int legitBins)
{
    const auto p = powerSpectrum (y);
    double leg = 0.0, al = 0.0;
    for (size_t k = 1; k < p.size(); ++k)
    {
        if ((double) k * fs / (double) y.size() > 20000.0) break;
        if ((int) k % legitBins == 0) leg += p[k]; else al += p[k];
    }
    return 10.0 * std::log10 (al / leg + 1e-30);
}

} // namespace jidai::test
