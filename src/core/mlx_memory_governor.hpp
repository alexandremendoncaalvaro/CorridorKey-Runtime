#pragma once

#include <cstddef>

namespace corridorkey::core::mlx_memory {

// Snapshot of MLX's memory accounting at a point in time. All values are in
// bytes. Fields map directly to mlx::core::get_*_memory() / get_*_limit().
// On non-MLX builds every field is zero.
//
// Header tidy-suppression rationale (file-level NOLINT block).
// See similar comments at the top of other src/ headers for the
// canonical reasoning (hot-path operator[], pixel-coord conventions,
// C-ABI signatures, validated-index access, etc.).
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,performance-no-int-to-ptr,bugprone-suspicious-include,cppcoreguidelines-avoid-do-while,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)

struct Snapshot {
    std::size_t active_bytes = 0;
    std::size_t cache_bytes = 0;
    std::size_t peak_bytes = 0;
    std::size_t memory_limit_bytes = 0;
    std::size_t cache_limit_bytes = 0;
    std::size_t wired_limit_bytes = 0;
    std::size_t max_recommended_working_set_bytes = 0;
};

// Policy selector for apply_policy(). Normal is the baseline applied by
// initialize_defaults(). Warn/Critical tighten the cache so MLX cooperates
// with the macOS VM compressor instead of fighting it.
enum class Policy {
    Normal,
    PressureWarn,
    PressureCritical,
};

// Apply the baseline MLX memory limits for this device. Safe to call
// repeatedly (std::once_flag underneath); only the first call installs the
// limits. Returns the snapshot captured after configuration.
//
// Why: MLX's defaults (memory_limit = 1.5x max recommended working set,
// cache_limit = memory_limit, wired_limit = 0) are tuned for 32 GB+ Apple
// Silicon machines running MLX in isolation. On 16 GB machines running
// alongside Resolve + a browser, the default cache grows into the VM
// compressor's working set and the compressor then steals GPU-queue
// bandwidth from MLX's next Metal submit (documented 50-108 s per-frame
// stalls, see mlx-lm #883 and our own Resolve log analysis).
Snapshot initialize_defaults();

// Ask MLX to release cached-but-unused allocations back to the system.
// Cheap; safe to call from a dispatch memory-pressure handler, from the
// session broker when it destroys a session, or between bridge changes.
void clear_cache();

// Re-read MLX's memory accounting without changing any limits. Intended for
// per-request telemetry in the runtime-server log.
Snapshot snapshot();

// Install an alternate policy and return the post-change snapshot. Normal
// restores the initialize_defaults() values; Warn tightens cache_limit to a
// small fraction of memory_limit and calls clear_cache(); Critical zeroes
// the cache entirely.
Snapshot apply_policy(Policy policy);

// Read the policy that apply_policy() last installed. The session broker
// consults this before compiling a new MLX shape: while the dispatch memory-
// pressure source is reporting Warn or Critical, prewarm is skipped so we do
// not commit a large allocation on top of a system that is already thrashing.
// Safe to call concurrently with apply_policy(); the underlying storage is a
// std::atomic.
Policy current_policy();

}  // namespace corridorkey::core::mlx_memory

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,performance-no-int-to-ptr,bugprone-suspicious-include,cppcoreguidelines-avoid-do-while,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)
