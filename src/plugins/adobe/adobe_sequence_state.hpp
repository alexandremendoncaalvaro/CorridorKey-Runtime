#pragma once

#include <cstdint>

namespace corridorkey::adobe {

inline constexpr std::uint32_t kAdobeSequenceStateVersion = 1;

struct AdobeSequenceState {
    std::uint32_t version = kAdobeSequenceStateVersion;
    std::uint32_t reserved = 0;
    std::uint64_t runtime_client_key = 0;
};

}  // namespace corridorkey::adobe
