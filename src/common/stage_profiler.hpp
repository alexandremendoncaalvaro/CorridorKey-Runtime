#pragma once

#include <chrono>
#include <corridorkey/types.hpp>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace corridorkey::common {

class StageProfiler {
   public:
    void record(const StageTiming& sample) {
        std::scoped_lock lock(m_mutex);

        for (auto& stage : m_stages) {
            if (stage.name != sample.name) continue;

            stage.total_ms += sample.total_ms;
            stage.sample_count += sample.sample_count;
            stage.work_units += sample.work_units;
            return;
        }

        m_stages.push_back(sample);
    }

    void record(std::string_view name, double total_ms, std::uint64_t work_units = 0) {
        record(StageTiming{std::string(name), total_ms, 1, work_units});
    }

    template <typename Function>
    decltype(auto) measure(std::string_view name, Function&& function,
                           std::uint64_t work_units = 0) {
        auto start = std::chrono::steady_clock::now();

        if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
            try {
                std::forward<Function>(function)();
            } catch (...) {
                auto end = std::chrono::steady_clock::now();
                record(name, elapsed_ms(start, end), work_units);
                throw;
            }
            auto end = std::chrono::steady_clock::now();
            record(name, elapsed_ms(start, end), work_units);
        } else {
            try {
                auto result = std::forward<Function>(function)();
                auto end = std::chrono::steady_clock::now();
                record(name, elapsed_ms(start, end), work_units);
                return result;
            } catch (...) {
                auto end = std::chrono::steady_clock::now();
                record(name, elapsed_ms(start, end), work_units);
                throw;
            }
        }
    }

    [[nodiscard]] std::vector<StageTiming> snapshot() const {
        std::scoped_lock lock(m_mutex);
        return m_stages;
    }

   private:
    static double elapsed_ms(const std::chrono::steady_clock::time_point& start,
                             const std::chrono::steady_clock::time_point& end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    mutable std::mutex m_mutex;
    std::vector<StageTiming> m_stages = {};
};

template <typename Function>
decltype(auto) measure_stage(const StageTimingCallback& callback, std::string_view name,
                             Function&& function, std::uint64_t work_units = 0) {
    auto start = std::chrono::steady_clock::now();

    if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
        try {
            std::forward<Function>(function)();
        } catch (...) {
            auto end = std::chrono::steady_clock::now();
            if (callback) {
                callback(StageTiming{
                    std::string(name),
                    std::chrono::duration<double, std::milli>(end - start).count(),
                    1,
                    work_units,
                });
            }
            throw;
        }
        auto end = std::chrono::steady_clock::now();
        if (callback) {
            callback(StageTiming{
                std::string(name),
                std::chrono::duration<double, std::milli>(end - start).count(),
                1,
                work_units,
            });
        }
    } else {
        try {
            auto result = std::forward<Function>(function)();
            auto end = std::chrono::steady_clock::now();
            if (callback) {
                callback(StageTiming{
                    std::string(name),
                    std::chrono::duration<double, std::milli>(end - start).count(),
                    1,
                    work_units,
                });
            }
            return result;
        } catch (...) {
            auto end = std::chrono::steady_clock::now();
            if (callback) {
                callback(StageTiming{
                    std::string(name),
                    std::chrono::duration<double, std::milli>(end - start).count(),
                    1,
                    work_units,
                });
            }
            throw;
        }
    }
}

}  // namespace corridorkey::common
