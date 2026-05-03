#include "mlx_session.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <regex>

#include "common/accelerate_utils.hpp"
#include "common/runtime_paths.hpp"
#include "common/stage_profiler.hpp"
#include "mlx_memory_governor.hpp"
#include "post_process/color_utils.hpp"

#if CORRIDORKEY_WITH_MLX
#include <mlx/mlx.h>
#endif

namespace corridorkey::core {

// NOLINTBEGIN(modernize-use-designated-initializers,readability-function-size,readability-function-cognitive-complexity,bugprone-easily-swappable-parameters,performance-unnecessary-value-param,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
//
// mlx_session.cpp tidy-suppression rationale.
//
// MlxSession is the Apple-side hot-path inference wrapper. The Error{}
// aggregate is used uniformly across every failure path; the inference
// methods (infer / infer_tile) carry size and complexity that reflects
// the explicit stage instrumentation contract (downscale -> normalise
// -> MLX run -> eval -> wait -> upscale), not accidental complexity.
// StageTimingCallback is a std::function that crosses lambda boundaries
// repeatedly inside measure_stage - the by-value parameter mirrors the
// engine-wide signature. Image rgb / alpha_hint pairs are deliberately
// adjacent because the call sites pre-bind them. Static-method tidy
// fires only on the no-MLX stub branch where the body short-circuits to
// an Error - the live MLX path uses m_impl. The fixed [3] mean / stddev
// arrays mirror the Accelerate normalise_and_pack_4ch C ABI.
#if CORRIDORKEY_WITH_MLX

// These helpers are only used from the CORRIDORKEY_WITH_MLX=1 path below.
// Guarding the entire anonymous namespace here avoids -Wunused-function errors
// on builds that stub out the MLX backend (e.g. macOS CI portable preset).
namespace {

std::optional<int> bridge_resolution_from_filename(const std::filesystem::path& path) {
    static const std::regex pattern(".*_bridge_([0-9]+)\\.mlxfn$");
    std::smatch match;
    auto filename = path.filename().string();
    if (!std::regex_match(filename, match, pattern) || match.size() != 2) {
        return std::nullopt;
    }

    return std::stoi(match[1].str());
}

std::vector<int> bridge_resolution_order() {
    return {512, 768, 1024, 1536, 2048};
}

std::vector<std::filesystem::path> bridge_candidates(const std::filesystem::path& model_path,
                                                     const DeviceInfo& device) {
    std::vector<std::filesystem::path> candidates;
    auto stem = model_path.stem().string();
    auto parent = model_path.parent_path();
    (void)device;
    for (int resolution : bridge_resolution_order()) {
        candidates.push_back(parent / (stem + "_bridge_" + std::to_string(resolution) + ".mlxfn"));
    }
    return candidates;
}

Result<std::filesystem::path> resolve_executable_artifact(const std::filesystem::path& model_path,
                                                          const DeviceInfo& device) {
    const auto requested_artifact = common::inspect_model_artifact(model_path);
    if (!requested_artifact.found) {
        return Unexpected<Error>{Error{ErrorCode::ModelLoadFailed,
                                       "MLX model artifact not found: " + model_path.string()}};
    }
    if (!requested_artifact.usable) {
        return Unexpected<Error>{Error{ErrorCode::ModelLoadFailed, requested_artifact.detail}};
    }

    if (model_path.extension() == ".mlxfn") {
        return model_path;
    }

    if (model_path.extension() == ".safetensors") {
        std::optional<std::string> first_unusable_bridge_error;
        for (const auto& candidate : bridge_candidates(model_path, device)) {
            const auto bridge_artifact = common::inspect_model_artifact(candidate);
            if (bridge_artifact.usable) {
                return candidate;
            }
            if (bridge_artifact.found && !first_unusable_bridge_error.has_value()) {
                first_unusable_bridge_error = bridge_artifact.detail;
            }
        }

        if (first_unusable_bridge_error.has_value()) {
            return Unexpected<Error>{
                Error{ErrorCode::ModelLoadFailed, *first_unusable_bridge_error}};
        }

        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  "MLX weights pack requires bridge exports. Run "
                  "scripts/prepare_mlx_model_pack.py to materialize the Apple model pack."}};
    }

    return Unexpected<Error>{
        Error{ErrorCode::ModelLoadFailed,
              "Unsupported MLX artifact. Use a .safetensors pack or a .mlxfn bridge: " +
                  model_path.string()}};
}

int resolved_model_resolution(const std::filesystem::path& executable_artifact_path) {
    if (auto resolution = bridge_resolution_from_filename(executable_artifact_path);
        resolution.has_value()) {
        return *resolution;
    }
    return 512;
}

void ensure_buffer_shape(ImageBuffer& buffer, int width, int height, int channels) {
    Image view = buffer.view();
    if (view.width == width && view.height == height && view.channels == channels) {
        return;
    }

    buffer = ImageBuffer(width, height, channels);
}

}  // namespace

#endif  // CORRIDORKEY_WITH_MLX

class MlxSession::Impl {
   public:
    int model_resolution = 512;
    ImageBuffer input_buffer;
    ImageBuffer alpha_buffer;
    ImageBuffer foreground_buffer;
    ImageBuffer prepared_rgb;
    ImageBuffer prepared_hint;
    ColorUtils::State color_utils_state;

#if CORRIDORKEY_WITH_MLX
    std::optional<mlx::core::ImportedFunction> imported_function = std::nullopt;
    std::optional<std::function<std::vector<mlx::core::array>(const mlx::core::Args&)>>
        compiled_function = std::nullopt;
#endif
};

MlxSession::MlxSession() : m_impl(std::make_unique<Impl>()) {}

MlxSession::~MlxSession() = default;
MlxSession::MlxSession(MlxSession&&) noexcept = default;
MlxSession& MlxSession::operator=(MlxSession&&) noexcept = default;

Result<std::unique_ptr<MlxSession>> MlxSession::create(const std::filesystem::path& model_path,
                                                       const DeviceInfo& device,
                                                       StageTimingCallback on_stage) {
#if !CORRIDORKEY_WITH_MLX
    (void)model_path;
    (void)device;
    (void)on_stage;
    return Unexpected<Error>{
        Error{ErrorCode::HardwareNotSupported,
              "MLX backend is not linked in this build. Reconfigure CMake with MLX available."}};
#else
    auto executable_artifact_res = common::measure_stage(
        on_stage, "mlx_artifact_resolve",
        [&]() { return resolve_executable_artifact(model_path, device); }, 1);
    if (!executable_artifact_res) {
        return Unexpected(executable_artifact_res.error());
    }

    // Belt-and-suspenders: make sure the process-wide MLX memory limits are in
    // place before the bridge is imported. initialize_defaults() is idempotent
    // via std::call_once, so paying for this here when the OFX runtime service
    // has already called it costs one atomic check.
    common::measure_stage(
        on_stage, "mlx_memory_init", [&]() { (void)mlx_memory::initialize_defaults(); }, 1);

    try {
        auto session = std::unique_ptr<MlxSession>(new MlxSession());
        session->m_impl->model_resolution = resolved_model_resolution(*executable_artifact_res);
        common::measure_stage(
            on_stage, "mlx_buffer_alloc",
            [&]() {
                session->m_impl->input_buffer = ImageBuffer(session->m_impl->model_resolution,
                                                            session->m_impl->model_resolution, 4);
                session->m_impl->alpha_buffer = ImageBuffer(session->m_impl->model_resolution,
                                                            session->m_impl->model_resolution, 1);
                session->m_impl->foreground_buffer = ImageBuffer(
                    session->m_impl->model_resolution, session->m_impl->model_resolution, 3);
            },
            1);
        common::measure_stage(
            on_stage, "mlx_bridge_import",
            [&]() {
                session->m_impl->imported_function.emplace(
                    mlx::core::import_function(executable_artifact_res->string()));
            },
            1);
        common::measure_stage(
            on_stage, "mlx_jit_compile",
            [&]() {
                std::function<std::vector<mlx::core::array>(const mlx::core::Args&)>
                    imported_callable = [imported = *session->m_impl->imported_function](
                                            const mlx::core::Args& args) { return imported(args); };
                session->m_impl->compiled_function.emplace(
                    mlx::core::compile(std::move(imported_callable), false));
            },
            1);
        return session;
    } catch (const std::exception& error) {
        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  "Failed to import MLX bridge function: " + std::string(error.what())}};
    }
#endif
}

Result<FrameResult> MlxSession::infer(const Image& rgb, const Image& alpha_hint,
                                      bool output_alpha_only, UpscaleMethod upscale_method,
                                      StageTimingCallback on_stage) {
    (void)upscale_method;
#if !CORRIDORKEY_WITH_MLX
    (void)rgb;
    (void)alpha_hint;
    (void)output_alpha_only;
    (void)on_stage;
    return Unexpected<Error>{
        Error{ErrorCode::HardwareNotSupported,
              "MLX backend is not linked in this build. Reconfigure CMake with MLX available."}};
#else
    if (!m_impl->imported_function.has_value()) {
        return Unexpected<Error>{
            Error{ErrorCode::InferenceFailed, "MLX bridge function is not initialized."}};
    }

    try {
        int model_res = m_impl->model_resolution;

        // Downscale from the host frame size (e.g. 1920x1080) to the model
        // resolution (e.g. 1024x1024). At 1920x1080 -> 1024 this is CPU-
        // bound area resampling and was invisible until this instrumentation
        // landed. Measuring it separately keeps mlx_prepare_inputs honest.
        ensure_buffer_shape(m_impl->prepared_rgb, model_res, model_res, 3);
        common::measure_stage(
            on_stage, "mlx_downscale_rgb",
            [&]() {
                ColorUtils::resize_area_into(rgb, m_impl->prepared_rgb.view(),
                                             m_impl->color_utils_state);
            },
            1);

        ensure_buffer_shape(m_impl->prepared_hint, model_res, model_res, 1);
        common::measure_stage(
            on_stage, "mlx_downscale_hint",
            [&]() {
                ColorUtils::resize_area_into(alpha_hint, m_impl->prepared_hint.view(),
                                             m_impl->color_utils_state);
            },
            1);

        Image input = m_impl->input_buffer.view();
        Image rgb_view = m_impl->prepared_rgb.view();
        Image hint_view = m_impl->prepared_hint.view();

        common::measure_stage(
            on_stage, "mlx_prepare_inputs",
            [&]() {
                const float means[3] = {0.485F, 0.456F, 0.406F};
                const float inv_stddevs[3] = {1.0F / 0.229F, 1.0F / 0.224F, 1.0F / 0.225F};
                common::accelerate_normalize_and_pack_4ch(
                    rgb_view.data.data(), 3, hint_view.data.data(), 1, input.data.data(),
                    input.width * input.height, means, inv_stddevs);
            },
            1);

        auto no_op = [](void*) {};
        mlx::core::Args args;
        args.emplace_back(
            input.data.data(),
            mlx::core::Shape{1, m_impl->model_resolution, m_impl->model_resolution, 4},
            mlx::core::float32, no_op);

        auto outputs = common::measure_stage(
            on_stage, "mlx_run",
            [&]() {
                if (m_impl->compiled_function.has_value()) {
                    try {
                        return (*m_impl->compiled_function)(args);
                    } catch (const std::exception&) {
                        m_impl->compiled_function.reset();
                    }
                }
                return (*m_impl->imported_function)(args);
            },
            1);
        if (outputs.empty()) {
            return Unexpected<Error>{
                Error{ErrorCode::InferenceFailed, "MLX bridge returned no output tensors."}};
        }

        auto alpha = mlx::core::contiguous(outputs[0]);
        std::optional<mlx::core::array> foreground = std::nullopt;
        if (!output_alpha_only) {
            if (outputs.size() < 2) {
                return Unexpected<Error>{
                    Error{ErrorCode::InferenceFailed,
                          "MLX bridge returned fewer than two output tensors."}};
            }
            foreground.emplace(mlx::core::contiguous(outputs[1]));
        }

        common::measure_stage(
            on_stage, "mlx_extract_outputs",
            [&]() {
                common::measure_stage(
                    on_stage, "mlx_materialize_outputs",
                    [&]() {
                        // Split eval (submission/graph evaluation) from wait
                        // (blocking for GPU completion). When the host is
                        // preempting the Metal command queue the cost lands
                        // in mlx_wait_alpha / mlx_wait_fg. When the cost is
                        // actual graph work it lands in mlx_eval.
                        common::measure_stage(
                            on_stage, "mlx_eval",
                            [&]() {
                                if (foreground.has_value()) {
                                    mlx::core::eval(alpha, *foreground);
                                } else {
                                    mlx::core::eval(alpha);
                                }
                            },
                            1);
                        common::measure_stage(
                            on_stage, "mlx_wait_alpha", [&]() { alpha.wait(); }, 1);
                        if (foreground.has_value()) {
                            common::measure_stage(
                                on_stage, "mlx_wait_fg", [&]() { foreground->wait(); }, 1);
                        }
                    },
                    1);
            },
            1);

        const auto& alpha_shape = alpha.shape();
        if (alpha_shape.size() != 4 || alpha_shape[0] != 1 || alpha_shape[3] != 1) {
            return Unexpected<Error>{
                Error{ErrorCode::InferenceFailed, "MLX bridge returned unexpected tensor shapes."}};
        }
        if (foreground.has_value()) {
            const auto& fg_shape = foreground->shape();
            if (fg_shape.size() != 4 || fg_shape[0] != 1 || fg_shape[3] != 3) {
                return Unexpected<Error>{Error{ErrorCode::InferenceFailed,
                                               "MLX bridge returned unexpected tensor shapes."}};
            }
        }

        int output_height = static_cast<int>(alpha_shape[1]);
        int output_width = static_cast<int>(alpha_shape[2]);

        ensure_buffer_shape(m_impl->alpha_buffer, output_width, output_height, 1);

        Image full_alpha = m_impl->alpha_buffer.view();
        common::measure_stage(
            on_stage, "mlx_copy_outputs",
            [&]() {
                const float low = 0.0F;
                const float high = 1.0F;
                std::memcpy(full_alpha.data.data(), alpha.data<float>(),
                            full_alpha.data.size_bytes());
                common::accelerate_vclip(full_alpha.data.data(), 1, &low, &high,
                                         full_alpha.data.data(), 1, full_alpha.data.size());

                if (foreground.has_value()) {
                    ensure_buffer_shape(m_impl->foreground_buffer, output_width, output_height, 3);
                    Image full_fg = m_impl->foreground_buffer.view();
                    std::memcpy(full_fg.data.data(), foreground->data<float>(),
                                full_fg.data.size_bytes());
                }
            },
            1);

        FrameResult result;
        bool use_lanczos = upscale_method == UpscaleMethod::Lanczos4;

        // CPU-side upscale from model_resolution (e.g. 1024) to the host
        // frame size (e.g. 1920x1080). Previously this was not instrumented,
        // so any regression here would be invisible to support. Stage name
        // reflects whether Lanczos or nearest was chosen.
        const char* upscale_stage_alpha =
            use_lanczos ? "mlx_upscale_alpha_lanczos" : "mlx_upscale_alpha_nearest";
        const char* upscale_stage_fg =
            use_lanczos ? "mlx_upscale_fg_lanczos" : "mlx_upscale_fg_nearest";

        common::measure_stage(
            on_stage, upscale_stage_alpha,
            [&]() {
                result.alpha = use_lanczos
                                   ? ColorUtils::resize_lanczos(full_alpha, rgb.width, rgb.height,
                                                                m_impl->color_utils_state)
                                   : ColorUtils::resize(full_alpha, rgb.width, rgb.height);
                ColorUtils::clamp_image(result.alpha.view(), 0.0F, 1.0F);
            },
            1);
        if (foreground.has_value()) {
            Image full_fg = m_impl->foreground_buffer.view();
            common::measure_stage(
                on_stage, upscale_stage_fg,
                [&]() {
                    result.foreground =
                        use_lanczos ? ColorUtils::resize_lanczos(full_fg, rgb.width, rgb.height,
                                                                 m_impl->color_utils_state)
                                    : ColorUtils::resize(full_fg, rgb.width, rgb.height);
                },
                1);
        }
        return result;
    } catch (const std::exception& error) {
        return Unexpected<Error>{Error{ErrorCode::InferenceFailed, "MLX bridge execution failed: " +
                                                                       std::string(error.what())}};
    }
#endif
}

Result<FrameResult> MlxSession::infer_tile(const Image& rgb_tile, const Image& hint_tile,
                                           bool output_alpha_only, StageTimingCallback on_stage) {
#if !CORRIDORKEY_WITH_MLX
    (void)rgb_tile;
    (void)hint_tile;
    (void)output_alpha_only;
    (void)on_stage;
    return Unexpected<Error>{
        Error{ErrorCode::HardwareNotSupported,
              "MLX backend is not linked in this build. Reconfigure CMake with MLX available."}};
#else
    if (!m_impl->imported_function.has_value()) {
        return Unexpected<Error>{
            Error{ErrorCode::InferenceFailed, "MLX bridge function is not initialized."}};
    }

    try {
        int model_res = m_impl->model_resolution;
        ensure_buffer_shape(m_impl->input_buffer, model_res, model_res, 4);
        Image input = m_impl->input_buffer.view();

        common::measure_stage(
            on_stage, "mlx_prepare_inputs",
            [&]() {
                const float means[3] = {0.485F, 0.456F, 0.406F};
                const float inv_stddevs[3] = {1.0F / 0.229F, 1.0F / 0.224F, 1.0F / 0.225F};
                common::accelerate_normalize_and_pack_4ch(
                    rgb_tile.data.data(), 3, hint_tile.data.data(), 1, input.data.data(),
                    model_res * model_res, means, inv_stddevs);
            },
            1);

        auto no_op = [](void*) {};
        mlx::core::Args args;
        args.emplace_back(input.data.data(), mlx::core::Shape{1, model_res, model_res, 4},
                          mlx::core::float32, no_op);

        auto outputs = common::measure_stage(
            on_stage, "mlx_run",
            [&]() {
                if (m_impl->compiled_function.has_value()) {
                    try {
                        return (*m_impl->compiled_function)(args);
                    } catch (const std::exception&) {
                        m_impl->compiled_function.reset();
                    }
                }
                return (*m_impl->imported_function)(args);
            },
            1);
        if (outputs.empty()) {
            return Unexpected<Error>{
                Error{ErrorCode::InferenceFailed, "MLX bridge returned no output tensors."}};
        }

        auto alpha = mlx::core::contiguous(outputs[0]);
        std::optional<mlx::core::array> foreground = std::nullopt;
        if (!output_alpha_only) {
            if (outputs.size() < 2) {
                return Unexpected<Error>{
                    Error{ErrorCode::InferenceFailed,
                          "MLX bridge returned fewer than two output tensors."}};
            }
            foreground.emplace(mlx::core::contiguous(outputs[1]));
        }

        common::measure_stage(
            on_stage, "mlx_eval_tile",
            [&]() {
                if (foreground.has_value()) {
                    mlx::core::eval(alpha, *foreground);
                } else {
                    mlx::core::eval(alpha);
                }
            },
            1);
        common::measure_stage(on_stage, "mlx_wait_alpha_tile", [&]() { alpha.wait(); }, 1);
        if (foreground.has_value()) {
            common::measure_stage(on_stage, "mlx_wait_fg_tile", [&]() { foreground->wait(); }, 1);
        }

        const auto& alpha_shape = alpha.shape();
        if (alpha_shape.size() != 4 || alpha_shape[0] != 1 || alpha_shape[3] != 1) {
            return Unexpected<Error>{
                Error{ErrorCode::InferenceFailed, "MLX bridge returned unexpected tensor shapes."}};
        }
        if (foreground.has_value()) {
            const auto& fg_shape = foreground->shape();
            if (fg_shape.size() != 4 || fg_shape[0] != 1 || fg_shape[3] != 3) {
                return Unexpected<Error>{Error{ErrorCode::InferenceFailed,
                                               "MLX bridge returned unexpected tensor shapes."}};
            }
        }

        int out_h = static_cast<int>(alpha_shape[1]);
        int out_w = static_cast<int>(alpha_shape[2]);

        FrameResult result;
        result.alpha = ImageBuffer(out_w, out_h, 1);

        const float low = 0.0F;
        const float high = 1.0F;
        std::memcpy(result.alpha.view().data.data(), alpha.data<float>(),
                    result.alpha.view().data.size_bytes());
        common::accelerate_vclip(result.alpha.view().data.data(), 1, &low, &high,
                                 result.alpha.view().data.data(), 1,
                                 result.alpha.view().data.size());

        if (foreground.has_value()) {
            result.foreground = ImageBuffer(out_w, out_h, 3);
            std::memcpy(result.foreground.view().data.data(), foreground->data<float>(),
                        result.foreground.view().data.size_bytes());
        }

        return result;
    } catch (const std::exception& error) {
        return Unexpected<Error>{Error{ErrorCode::InferenceFailed,
                                       "MLX tile inference failed: " + std::string(error.what())}};
    }
#endif
}

int MlxSession::model_resolution() const {
    return m_impl->model_resolution;
}

}  // namespace corridorkey::core
// NOLINTEND(modernize-use-designated-initializers,readability-function-size,readability-function-cognitive-complexity,bugprone-easily-swappable-parameters,performance-unnecessary-value-param,readability-convert-member-functions-to-static,cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
