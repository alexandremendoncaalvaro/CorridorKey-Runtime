#pragma once

#include <cstddef>
#include <optional>
#include <utility>

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
#include <cuda_runtime_api.h>
#endif

namespace corridorkey::core {

template <typename T>
class PinnedBuffer {
   public:
    PinnedBuffer() = default;

    ~PinnedBuffer() {
        release();
    }

    PinnedBuffer(const PinnedBuffer&) = delete;
    PinnedBuffer& operator=(const PinnedBuffer&) = delete;

    PinnedBuffer(PinnedBuffer&& other) noexcept : m_size(other.m_size), m_data(other.m_data) {
        other.m_size = 0;
        other.m_data = nullptr;
    }

    PinnedBuffer& operator=(PinnedBuffer&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        release();
        m_size = other.m_size;
        m_data = other.m_data;
        other.m_size = 0;
        other.m_data = nullptr;
        return *this;
    }

    [[nodiscard]] static std::optional<PinnedBuffer> try_allocate(std::size_t element_count) {
        if (element_count == 0) {
            return PinnedBuffer{};
        }
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
        void* ptr = nullptr;
        auto status = cudaMallocHost(&ptr, element_count * sizeof(T));
        if (status != cudaSuccess || ptr == nullptr) {
            return std::nullopt;
        }
        PinnedBuffer buf;
        buf.m_data = static_cast<T*>(ptr);
        buf.m_size = element_count;
        return buf;
#else
        (void)element_count;
        return std::nullopt;
#endif
    }

    [[nodiscard]] T* data() {
        return m_data;
    }

    [[nodiscard]] const T* data() const {
        return m_data;
    }

    [[nodiscard]] std::size_t size() const {
        return m_size;
    }

    [[nodiscard]] bool empty() const {
        return m_data == nullptr;
    }

   private:
    void release() {
        if (m_data == nullptr) {
            return;
        }
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
        cudaFreeHost(m_data);
#endif
        m_data = nullptr;
        m_size = 0;
    }

    std::size_t m_size = 0;
    T* m_data = nullptr;
};

}  // namespace corridorkey::core
