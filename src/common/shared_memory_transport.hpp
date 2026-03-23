#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace corridorkey::common {

inline constexpr std::uint32_t kOfxFrameTransportMagic = 0x434B4658U;
inline constexpr std::uint32_t kOfxFrameTransportVersion = 1U;

struct SharedFrameTransportHeader {
    std::uint32_t magic = kOfxFrameTransportMagic;
    std::uint32_t version = kOfxFrameTransportVersion;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rgb_channels = 3;
    std::uint32_t hint_channels = 1;
    std::uint32_t alpha_channels = 1;
    std::uint32_t foreground_channels = 3;
    std::uint64_t rgb_offset = 0;
    std::uint64_t hint_offset = 0;
    std::uint64_t alpha_offset = 0;
    std::uint64_t foreground_offset = 0;
    std::uint64_t total_bytes = 0;
};

class CORRIDORKEY_API SharedFrameTransport {
   public:
    static Result<SharedFrameTransport> create(const std::filesystem::path& path, int width,
                                               int height);
    static Result<SharedFrameTransport> open(const std::filesystem::path& path);

    SharedFrameTransport();
    ~SharedFrameTransport();

    SharedFrameTransport(const SharedFrameTransport&) = delete;
    SharedFrameTransport& operator=(const SharedFrameTransport&) = delete;
    SharedFrameTransport(SharedFrameTransport&& other) noexcept;
    SharedFrameTransport& operator=(SharedFrameTransport&& other) noexcept;

    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

    [[nodiscard]] Image rgb_view();
    [[nodiscard]] Image hint_view();
    [[nodiscard]] Image alpha_view();
    [[nodiscard]] Image foreground_view();

   private:
    static std::uint64_t payload_float_count(int width, int height, int channels);
    static std::size_t mapped_size_for_dimensions(int width, int height);
    static SharedFrameTransportHeader build_header(int width, int height);

    Result<void> map_new_file(const std::filesystem::path& path, std::size_t size);
    Result<void> map_existing_file(const std::filesystem::path& path);
    Result<void> finalize_header();
    Result<void> validate_header() const;
    void close();

    [[nodiscard]] float* float_data_at(std::uint64_t byte_offset) const;

    std::filesystem::path m_path = {};
    std::byte* m_mapping = nullptr;
    std::size_t m_mapping_size = 0;
    SharedFrameTransportHeader* m_header = nullptr;
#if defined(_WIN32)
    void* m_file_handle = nullptr;
    void* m_mapping_handle = nullptr;
#else
    int m_fd = -1;
#endif
};

CORRIDORKEY_API std::filesystem::path next_ofx_shared_frame_path();

}  // namespace corridorkey::common
