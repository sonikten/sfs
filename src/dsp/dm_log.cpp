// src/dsp/dm_log.cpp

#include "dm_log.h"

#include <cmath>

namespace sfs::dsp
{

namespace
{

constexpr float kLn2 = 0.6931471805599453f;

} // namespace

float dm_log(float x) noexcept
{
    // x = m · 2^e via std::frexp (m in [0.5, 1), e is the IEEE exponent
    // shifted so x reconstructs via std::ldexp(m, e)).
    int e = 0;
    float m = std::frexp(x, &e);

    // Shift to put m in [1, 2): m *= 2; e -= 1.
    m *= 2.0f;
    e -= 1;

    // Use log(m) = 2·atanh((m-1)/(m+1)). For m in [1, 2), z = (m-1)/(m+1)
    // is bounded in [0, 1/3] — much narrower than y = m-1 in [0, 1) — so
    // the atanh series converges fast.
    //
    //   atanh(z) = z + z³/3 + z⁵/5 + z⁷/7 + z⁹/9 + ...
    //
    // 5 terms on [0, 1/3]: worst-case error ~5e-7 absolute, well below
    // the spec's 5e-5 budget.
    const float z = (m - 1.0f) / (m + 1.0f);
    const float z2 = z * z;
    const float z3 = z2 * z;
    const float z5 = z3 * z2;
    const float z7 = z5 * z2;
    const float z9 = z7 * z2;
    const float atanhZ = z + z3 * (1.0f / 3.0f) + z5 * 0.2f + z7 * (1.0f / 7.0f) + z9 * (1.0f / 9.0f);
    const float logM = 2.0f * atanhZ;

    return logM + static_cast<float>(e) * kLn2;
}

} // namespace sfs::dsp
