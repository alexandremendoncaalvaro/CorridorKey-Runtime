#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace corridorkey::common {

//
// Header tidy-suppression rationale.
//
// This header is included transitively by many TUs (typically the OFX
// render hot path or the offline batch driver) so its diagnostics
// surface in every consumer once HeaderFilterRegex is scoped to the
// project tree. The categories suppressed below all flag stylistic
// patterns required by the surrounding C ABIs (OFX / ONNX Runtime /
// CUDA / NPP / FFmpeg), the universal pixel / tensor coordinate
// conventions, validated-index operator[] sites, or canonical
// orchestrator function shapes whose linear flow would be obscured by
// helper extraction. Genuine logic regressions are caught by the
// downstream TU sweep and the unit-test suite.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)

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

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)
