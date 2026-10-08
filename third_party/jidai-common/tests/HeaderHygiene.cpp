// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
// Every shared header in one TU, built with the strictest warning set the units use (-Wfloat-equal included), so a
// header never brings warnings into a unit that vendors it. Runs as the "headers" test.
#include "jidai/CableStandard.h"
#include "jidai/dsp/Halfband.h"
#include "jidai/dsp/TripleShaper.h"

int main()
{
    bool over = false;
    const float v = jidai::jcs::clampRail (7.0f, over);
    return (over && v > 4.9f) ? 0 : 1;
}
