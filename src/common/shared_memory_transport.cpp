#include "shared_memory_transport.hpp"

#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <thread>

#include "runtime_paths.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace corridorkey::common {

namespace {

Error transport_error(ErrorCode code, const std::string& message) {
    return Error{code, message};
}

#if defined(_WIN32)
constexpr DWORD kSharedFrameShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
#endif

#if defined(_WIN32)
std::wstring wide_path(const std::filesystem::path& path) {
    return path.wstring();
}
#endif

}  // namespace

SharedFrameTransport::SharedFrameTransport() = default;

SharedFrameTransport::~SharedFrameTransport() {
    close();
}

SharedFrameTransport::SharedFrameTransport(SharedFrameTransport&& other) noexcept {
    *this = std::move(other);
}

SharedFrameTransport& SharedFrameTransport::operator=(SharedFrameTransport&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    close();
    m_path = std::move(other.m_path);
    m_mapping = other.m_mapping;
    m_mapping_size = other.m_mapping_size;
    m_header = other.m_header;
#if defined(_WIN32)
    m_file_handle = other.m_file_handle;
    m_mapping_handle = other.m_mapping_handle;
    other.m_file_handle = nullptr;
    other.m_mapping_handle = nullptr;
#else
    m_fd = other.m_fd;
    other.m_fd = -1;
#endif
    other.m_mapping = nullptr;
    other.m_mapping_size = 0;
    other.m_header = nullptr;
    return *this;
}

Result<SharedFrameTransport> SharedFrameTransport::create(const std::filesystem::path& path,
                                                          int width, int height) {
    if (width <= 0 || height <= 0) {
        return Unexpected<Error>(transport_error(ErrorCode::InvalidParameters,
                                                 "Shared frame dimensions must be positive."));
    }

    SharedFrameTransport transport;
    auto size = mapped_size_for_dimensions(width, height);
    auto map_result = transport.map_new_file(path, size);
    if (!map_result) {
        return Unexpected<Error>(map_result.error());
    }

    *transport.m_header = build_header(width, height);
    auto header_result = transport.finalize_header();
    if (!header_result) {
        return Unexpected<Error>(header_result.error());
    }

    return transport;
}

Result<SharedFrameTransport> SharedFrameTransport::open(const std::filesystem::path& path) {
    SharedFrameTransport transport;
    auto map_result = transport.map_existing_file(path);
    if (!map_result) {
        return Unexpected<Error>(map_result.error());
    }

    auto header_result = transport.validate_header();
    if (!header_result) {
        return Unexpected<Error>(header_result.error());
    }

    return transport;
}

std::uint64_t SharedFrameTransport::payload_float_count(int width, int height, int channels) {
    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) *
           static_cast<std::uint64_t>(channels);
}

std::size_t SharedFrameTransport::mapped_size_for_dimensions(int width, int height) {
    auto header = build_header(width, height);
    return static_cast<std::size_t>(header.total_bytes);
}

SharedFrameTransportHeader SharedFrameTransport::build_header(int width, int height) {
    SharedFrameTransportHeader header;
    header.width = static_cast<std::uint32_t>(width);
    header.height = static_cast<std::uint32_t>(height);
    header.rgb_offset = sizeof(SharedFrameTransportHeader);
    header.hint_offset =
        header.rgb_offset + payload_float_count(width, height, header.rgb_channels) * sizeof(float);
    header.alpha_offset = header.hint_offset +
                          payload_float_count(width, height, header.hint_channels) * sizeof(float);
    header.foreground_offset =
        header.alpha_offset +
        payload_float_count(width, height, header.alpha_channels) * sizeof(float);
    header.total_bytes =
        header.foreground_offset +
        payload_float_count(width, height, header.foreground_channels) * sizeof(float);
    return header;
}

Result<void> SharedFrameTransport::map_new_file(const std::filesystem::path& path,
                                                std::size_t size) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return Unexpected<Error>(transport_error(
            ErrorCode::IoError, "Failed to create shared frame directory: " + error.message()));
    }

#if defined(_WIN32)
    auto file_handle =
        CreateFileW(wide_path(path).c_str(), GENERIC_READ | GENERIC_WRITE, kSharedFrameShareMode,
                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file_handle == INVALID_HANDLE_VALUE) {
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to create shared frame file."));
    }

    LARGE_INTEGER desired_size;
    desired_size.QuadPart = static_cast<LONGLONG>(size);
    if (!SetFilePointerEx(file_handle, desired_size, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(file_handle)) {
        CloseHandle(file_handle);
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to size shared frame file."));
    }

    auto mapping_handle = CreateFileMappingW(file_handle, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (mapping_handle == nullptr) {
        CloseHandle(file_handle);
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to create file mapping for shared frame."));
    }

    auto mapping =
        static_cast<std::byte*>(MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, size));
    if (mapping == nullptr) {
        CloseHandle(mapping_handle);
        CloseHandle(file_handle);
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to map shared frame file."));
    }

    m_file_handle = file_handle;
    m_mapping_handle = mapping_handle;
#else
    int fd = ::open(path.string().c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to create shared frame file."));
    }
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        ::close(fd);
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to size shared frame file."));
    }
    auto mapping =
        static_cast<std::byte*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (mapping == MAP_FAILED) {
        ::close(fd);
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to map shared frame file."));
    }
    m_fd = fd;
#endif

    m_path = path;
    m_mapping_size = size;
    m_mapping = mapping;
    std::memset(m_mapping, 0, size);
    m_header = reinterpret_cast<SharedFrameTransportHeader*>(m_mapping);
    return {};
}

Result<void> SharedFrameTransport::map_existing_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return Unexpected<Error>(transport_error(
            ErrorCode::IoError, "Shared frame file does not exist: " + path.string()));
    }

#if defined(_WIN32)
    auto file_handle =
        CreateFileW(wide_path(path).c_str(), GENERIC_READ | GENERIC_WRITE, kSharedFrameShareMode,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_handle == INVALID_HANDLE_VALUE) {
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to open shared frame file."));
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file_handle, &file_size)) {
        CloseHandle(file_handle);
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to read shared frame size."));
    }

    auto mapping_handle = CreateFileMappingW(file_handle, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (mapping_handle == nullptr) {
        CloseHandle(file_handle);
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to create file mapping for shared frame."));
    }

    auto mapping =
        static_cast<std::byte*>(MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (mapping == nullptr) {
        CloseHandle(mapping_handle);
        CloseHandle(file_handle);
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to map shared frame file."));
    }

    m_file_handle = file_handle;
    m_mapping_handle = mapping_handle;
    m_mapping_size = static_cast<std::size_t>(file_size.QuadPart);
#else
    int fd = ::open(path.string().c_str(), O_RDWR);
    if (fd < 0) {
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to open shared frame file."));
    }

    struct stat stat_buffer {};
    if (fstat(fd, &stat_buffer) != 0) {
        ::close(fd);
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to read shared frame size."));
    }

    auto mapping =
        static_cast<std::byte*>(mmap(nullptr, static_cast<std::size_t>(stat_buffer.st_size),
                                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (mapping == MAP_FAILED) {
        ::close(fd);
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Failed to map shared frame file."));
    }

    m_fd = fd;
    m_mapping_size = static_cast<std::size_t>(stat_buffer.st_size);
#endif

    m_path = path;
    m_mapping = mapping;
    m_header = reinterpret_cast<SharedFrameTransportHeader*>(m_mapping);
    return {};
}

Result<void> SharedFrameTransport::finalize_header() {
    if (m_header == nullptr) {
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Shared frame header was not mapped."));
    }
    return validate_header();
}

Result<void> SharedFrameTransport::validate_header() const {
    if (m_header == nullptr || m_mapping == nullptr) {
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Shared frame mapping is not ready."));
    }
    if (m_header->magic != kOfxFrameTransportMagic ||
        m_header->version != kOfxFrameTransportVersion) {
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Shared frame header is invalid."));
    }
    if (m_header->total_bytes != m_mapping_size) {
        return Unexpected<Error>(
            transport_error(ErrorCode::IoError, "Shared frame size does not match header."));
    }
    return {};
}

void SharedFrameTransport::close() {
    if (m_mapping != nullptr) {
#if defined(_WIN32)
        UnmapViewOfFile(m_mapping);
#else
        munmap(m_mapping, m_mapping_size);
#endif
    }

#if defined(_WIN32)
    if (m_mapping_handle != nullptr) {
        CloseHandle(m_mapping_handle);
    }
    if (m_file_handle != nullptr) {
        CloseHandle(m_file_handle);
    }
    m_mapping_handle = nullptr;
    m_file_handle = nullptr;
#else
    if (m_fd >= 0) {
        ::close(m_fd);
    }
    m_fd = -1;
#endif
    m_mapping = nullptr;
    m_mapping_size = 0;
    m_header = nullptr;
}

const std::filesystem::path& SharedFrameTransport::path() const {
    return m_path;
}

int SharedFrameTransport::width() const {
    return m_header == nullptr ? 0 : static_cast<int>(m_header->width);
}

int SharedFrameTransport::height() const {
    return m_header == nullptr ? 0 : static_cast<int>(m_header->height);
}

float* SharedFrameTransport::float_data_at(std::uint64_t byte_offset) const {
    return reinterpret_cast<float*>(m_mapping + byte_offset);
}

Image SharedFrameTransport::rgb_view() {
    return Image{width(), height(), static_cast<int>(m_header->rgb_channels),
                 std::span<float>(float_data_at(m_header->rgb_offset),
                                  payload_float_count(width(), height(),
                                                      static_cast<int>(m_header->rgb_channels)))};
}

Image SharedFrameTransport::hint_view() {
    return Image{width(), height(), static_cast<int>(m_header->hint_channels),
                 std::span<float>(float_data_at(m_header->hint_offset),
                                  payload_float_count(width(), height(),
                                                      static_cast<int>(m_header->hint_channels)))};
}

Image SharedFrameTransport::alpha_view() {
    return Image{width(), height(), static_cast<int>(m_header->alpha_channels),
                 std::span<float>(float_data_at(m_header->alpha_offset),
                                  payload_float_count(width(), height(),
                                                      static_cast<int>(m_header->alpha_channels)))};
}

Image SharedFrameTransport::foreground_view() {
    return Image{
        width(), height(), static_cast<int>(m_header->foreground_channels),
        std::span<float>(float_data_at(m_header->foreground_offset),
                         payload_float_count(width(), height(),
                                             static_cast<int>(m_header->foreground_channels)))};
}

std::filesystem::path next_ofx_shared_frame_path() {
    auto root = ofx_runtime_shared_frames_root();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const auto time_seed = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto thread_seed =
        static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    auto seed = std::to_string(detail::fnv1a_64(root.string() + "|" + std::to_string(time_seed) +
                                                "|" + std::to_string(thread_seed) + "|" +
                                                std::to_string(std::time(nullptr))));
    return root / ("frame_" + seed + ".ckfx");
}

}  // namespace corridorkey::common
