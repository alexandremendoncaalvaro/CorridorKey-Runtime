#pragma once

#include <corridorkey/types.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace corridorkey::core {

struct NumericStats {
    std::size_t total_count = 0;
    std::size_t finite_count = 0;
    float min_value = 0.0F;
    float max_value = 0.0F;
    double mean_value = 0.0;
};

inline NumericStats compute_numeric_stats(std::span<const float> values) {
    NumericStats stats;
    stats.total_count = values.size();

    for (float value : values) {
        if (!std::isfinite(value)) {
            continue;
        }
        if (stats.finite_count == 0) {
            stats.min_value = value;
            stats.max_value = value;
        } else {
            stats.min_value = std::min(stats.min_value, value);
            stats.max_value = std::max(stats.max_value, value);
        }
        stats.mean_value += static_cast<double>(value);
        ++stats.finite_count;
    }

    if (stats.finite_count > 0) {
        stats.mean_value /= static_cast<double>(stats.finite_count);
    }

    return stats;
}

inline std::string format_numeric_stats(std::string_view label, const NumericStats& stats) {
    std::ostringstream stream;
    stream << label << " total=" << stats.total_count << " finite=" << stats.finite_count;
    if (stats.finite_count > 0) {
        stream << " min=" << stats.min_value << " max=" << stats.max_value
               << " mean=" << stats.mean_value;
    } else {
        stream << " min=n/a max=n/a mean=n/a";
    }
    return stream.str();
}

inline Result<void> validate_finite_values(std::span<const float> values, std::string_view label) {
    const NumericStats stats = compute_numeric_stats(values);
    if (stats.finite_count == stats.total_count) {
        return {};
    }

    return Unexpected(Error{
        ErrorCode::InferenceFailed,
        "Model output contains non-finite values. " + format_numeric_stats(label, stats),
    });
}

inline Result<void> validate_finite_image(Image image, std::string_view label) {
    return validate_finite_values(image.data, label);
}

}  // namespace corridorkey::core
