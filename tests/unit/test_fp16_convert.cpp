#include <catch2/catch_all.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "common/fp16_convert.hpp"

using namespace corridorkey::common;

namespace {

// Reference scalar FP16->FP32 using the same bit-exact formula
float scalar_fp16_to_fp32(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h >> 15) << 31;
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantissa = h & 0x3FF;
    uint32_t result = 0;
    if (exponent == 0) {
        if (mantissa != 0) {
            exponent = 127 - 14;
            while ((mantissa & 0x400) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3FF;
            result = sign | (exponent << 23) | (mantissa << 13);
        } else {
            result = sign;
        }
    } else if (exponent == 31) {
        result = sign | 0x7F800000 | (mantissa << 13);
    } else {
        result = sign | ((exponent + 112) << 23) | (mantissa << 13);
    }
    float value = 0.0F;
    std::memcpy(&value, &result, sizeof(value));
    return value;
}

}  // namespace

TEST_CASE("convert_fp16_to_fp32 matches scalar reference for normal values", "[unit]") {
    // Sample a range of normal FP16 values
    std::vector<uint16_t> inputs;
    std::vector<float> expected;
    // exponents 1..30, mantissa 0 (exact powers of 2 and fractions)
    for (uint16_t exp = 1; exp < 31; ++exp) {
        for (uint16_t mant : {uint16_t{0}, uint16_t{512}, uint16_t{1023}}) {
            uint16_t h = static_cast<uint16_t>((exp << 10) | mant);
            inputs.push_back(h);
            expected.push_back(scalar_fp16_to_fp32(h));
        }
    }

    std::vector<float> output(inputs.size(), 0.0F);
    convert_fp16_to_fp32(inputs.data(), output.data(), inputs.size());

    for (std::size_t i = 0; i < inputs.size(); ++i) {
        REQUIRE(output[i] == Catch::Approx(expected[i]).epsilon(1e-5F));
    }
}

TEST_CASE("convert_fp16_to_fp32 handles zero", "[unit]") {
    uint16_t pos_zero = 0x0000;
    uint16_t neg_zero = 0x8000;
    float out[2] = {};
    uint16_t inp[2] = {pos_zero, neg_zero};
    convert_fp16_to_fp32(inp, out, 2);
    REQUIRE(out[0] == 0.0F);
    REQUIRE(out[1] == 0.0F);
}

TEST_CASE("convert_fp16_to_fp32 handles infinity", "[unit]") {
    uint16_t pos_inf = 0x7C00;
    uint16_t neg_inf = 0xFC00;
    float out[2] = {};
    uint16_t inp[2] = {pos_inf, neg_inf};
    convert_fp16_to_fp32(inp, out, 2);
    REQUIRE(std::isinf(out[0]));
    REQUIRE(out[0] > 0.0F);
    REQUIRE(std::isinf(out[1]));
    REQUIRE(out[1] < 0.0F);
}

TEST_CASE("convert_fp16_to_fp32 handles NaN", "[unit]") {
    uint16_t nan_val = 0x7E00;  // quiet NaN
    float out = 0.0F;
    convert_fp16_to_fp32(&nan_val, &out, 1);
    REQUIRE(std::isnan(out));
}

TEST_CASE("convert_fp16_to_fp32 handles count not multiple of 8", "[unit]") {
    // 11 elements: exercises the SIMD path (8) plus scalar tail (3)
    constexpr std::size_t kCount = 11;
    uint16_t inputs[kCount];
    float expected[kCount];
    for (std::size_t i = 0; i < kCount; ++i) {
        // Use FP16 representation of i+1 (positive normal, exponent 15+0=15, value = 1..11)
        // Just use known FP16 for 1.0 = 0x3C00
        inputs[i] = static_cast<uint16_t>(0x3C00 + static_cast<uint16_t>(i));
        expected[i] = scalar_fp16_to_fp32(inputs[i]);
    }
    float output[kCount] = {};
    convert_fp16_to_fp32(inputs, output, kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        REQUIRE(output[i] == Catch::Approx(expected[i]).epsilon(1e-5F));
    }
}
