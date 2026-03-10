#pragma once

#include <algorithm>
#include <thread>
#include <vector>

namespace corridorkey::common {

template <typename Function>
void parallel_for_rows(int total_rows, Function&& function, int min_rows_per_worker = 64) {
    if (total_rows <= 0) {
        return;
    }

    unsigned int hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads <= 1 || total_rows < min_rows_per_worker) {
        function(0, total_rows);
        return;
    }

    int max_workers = std::max(1, total_rows / min_rows_per_worker);
    int worker_count = std::min<int>(static_cast<int>(hardware_threads), max_workers);
    if (worker_count <= 1) {
        function(0, total_rows);
        return;
    }

    int rows_per_worker = (total_rows + worker_count - 1) / worker_count;
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(worker_count - 1));

    for (int worker_index = 1; worker_index < worker_count; ++worker_index) {
        int begin = worker_index * rows_per_worker;
        int end = std::min(total_rows, begin + rows_per_worker);
        if (begin >= end) {
            break;
        }

        workers.emplace_back([begin, end, &function]() { function(begin, end); });
    }

    function(0, std::min(total_rows, rows_per_worker));

    for (auto& worker : workers) {
        worker.join();
    }
}

}  // namespace corridorkey::common
