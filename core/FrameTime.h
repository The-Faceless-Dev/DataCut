#pragma once

#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace datacut {

struct FrameRate {
    std::int64_t numerator = 30;
    std::int64_t denominator = 1;

    [[nodiscard]] bool valid() const { return numerator > 0 && denominator > 0; }
};

struct FrameRange {
    // Half-open source range: first is included, end is excluded.
    std::int64_t first = 0;
    std::int64_t end = 0;

    [[nodiscard]] bool valid() const { return first >= 0 && end > first; }
    [[nodiscard]] std::int64_t length() const { return end - first; }
};

inline std::int64_t rescaleFrames(std::int64_t frames, FrameRate from, FrameRate to)
{
    if (frames < 0 || !from.valid() || !to.valid())
        throw std::invalid_argument("Invalid frame conversion");

    // A source frame spans from.den/from.num seconds. Cancel factors before
    // multiplying so common video rates fit safely in signed 64-bit arithmetic.
    std::int64_t factors[3] = {frames, from.denominator, to.numerator};
    std::int64_t divisors[2] = {from.numerator, to.denominator};
    for (auto &factor : factors) {
        for (auto &divisor : divisors) {
            const auto common = std::gcd(factor, divisor);
            factor /= common;
            divisor /= common;
        }
    }
    auto multiply = [](std::int64_t a, std::int64_t b) {
        if (b != 0 && a > std::numeric_limits<std::int64_t>::max() / b)
            throw std::overflow_error("Frame conversion exceeds supported range");
        return a * b;
    };
    const auto numerator = multiply(multiply(factors[0], factors[1]), factors[2]);
    const auto denominator = multiply(divisors[0], divisors[1]);
    // Round half up without risking overflow in numerator + denominator/2.
    return numerator / denominator + (numerator % denominator >= (denominator + 1) / 2 ? 1 : 0);
}

} // namespace datacut
