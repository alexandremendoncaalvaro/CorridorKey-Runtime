#include "mlx_session.hpp"

#include <algorithm>
#include <optional>
#include <regex>

#include "common/stage_profiler.hpp"
#include "post_process/color_utils.hpp"

#if CORRIDORKEY_WITH_MLX
#include <mlx/mlx.h>
#endif

namespace corridorkey::core {

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

std::vector<std::filesystem::path> bridge_candidates(const std::filesystem::path& model_path) {
    std::vector<std::filesystem::path> candidates;
    auto stem = model_path.stem().string();
    auto parent = model_path.parent_path();
    // Prefer the smallest bridge first. The .mlxfn path is currently an
    // experimental runtime bridge, so bounded first-frame latency matters more
    // than silently selecting the largest available export.
    for (int resolution : {512, 768, 1024}) {
        candidates.push_back(parent / (stem + "_bridge_" + std::to_string(resolution) + ".mlxfn"));
    }
    return candidates;
}

Result<std::filesystem::path> resolve_executable_artifact(const std::filesystem::path& model_path) {
    if (!std::filesystem::exists(model_path)) {
        return Unexpected<Error>{Error{ErrorCode::ModelLoadFailed,
                                       "MLX model artifact not found: " + model_path.string()}};
    }

    if (model_path.extension() == ".mlxfn") {
        return model_path;
    }

    if (model_path.extension() == ".safetensors") {
        for (const auto& candidate : bridge_candidates(model_path)) {
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }

        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  "MLX weights pack requires a bridge .mlxfn artifact. Run "
                  "scripts/prepare_mlx_model_pack.py with --export-mlxfn to create one."}};
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

}  // namespace

class MlxSession::Impl {
   public:
    int model_resolution = 512;
    ImageBuffer input_buffer = {};

#if CORRIDORKEY_WITH_MLX
    std::optional<mlx::core::ImportedFunction> function = std::nullopt;
#endif
};

MlxSession::MlxSession() : m_impl(std::make_unique<Impl>()) {}

MlxSession::~MlxSession() = default;
MlxSession::MlxSession(MlxSession&&) noexcept = default;
MlxSession& MlxSession::operator=(MlxSession&&) noexcept = default;

Result<std::unique_ptr<MlxSession>> MlxSession::create(const std::filesystem::path& model_path) {
#if !CORRIDORKEY_WITH_MLX
    (void)model_path;
    return Unexpected<Error>{
        Error{ErrorCode::HardwareNotSupported,
              "MLX backend is not linked in this build. Reconfigure CMake with MLX available."}};
#else
    auto executable_artifact_res = resolve_executable_artifact(model_path);
    if (!executable_artifact_res) {
        return Unexpected(executable_artifact_res.error());
    }

    try {
        auto session = std::unique_ptr<MlxSession>(new MlxSession());
        session->m_impl->model_resolution = resolved_model_resolution(*executable_artifact_res);
        session->m_impl->input_buffer =
            ImageBuffer(session->m_impl->model_resolution, session->m_impl->model_resolution, 4);
        session->m_impl->function.emplace(
            mlx::core::import_function(executable_artifact_res->string()));
        return session;
    } catch (const std::exception& error) {
        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  "Failed to import MLX bridge function: " + std::string(error.what())}};
    }
#endif
}

Result<FrameResult> MlxSession::infer(const Image& rgb, const Image& alpha_hint,
                                      StageTimingCallback on_stage) {
#if !CORRIDORKEY_WITH_MLX
    (void)rgb;
    (void)alpha_hint;
    (void)on_stage;
    return Unexpected<Error>{
        Error{ErrorCode::HardwareNotSupported,
              "MLX backend is not linked in this build. Reconfigure CMake with MLX available."}};
#else
    if (!m_impl->function.has_value()) {
        return Unexpected<Error>{
            Error{ErrorCode::InferenceFailed, "MLX bridge function is not initialized."}};
    }

    try {
        auto [padded_rgb, rgb_roi] =
            ColorUtils::fit_pad(rgb, m_impl->model_resolution, m_impl->model_resolution);
        auto [padded_hint, hint_roi] =
            ColorUtils::fit_pad(alpha_hint, m_impl->model_resolution, m_impl->model_resolution);
        (void)hint_roi;

        Image input = m_impl->input_buffer.view();
        Image rgb_view = padded_rgb.view();
        Image hint_view = padded_hint.view();

        common::measure_stage(
            on_stage, "mlx_prepare_inputs",
            [&]() {
                for (int y_pos = 0; y_pos < input.height; ++y_pos) {
                    for (int x_pos = 0; x_pos < input.width; ++x_pos) {
                        input(y_pos, x_pos, 0) = (rgb_view(y_pos, x_pos, 0) - 0.485F) / 0.229F;
                        input(y_pos, x_pos, 1) = (rgb_view(y_pos, x_pos, 1) - 0.456F) / 0.224F;
                        input(y_pos, x_pos, 2) = (rgb_view(y_pos, x_pos, 2) - 0.406F) / 0.225F;
                        input(y_pos, x_pos, 3) = hint_view(y_pos, x_pos, 0);
                    }
                }
            },
            1);

        auto no_op = [](void*) {};
        mlx::core::Args args;
        args.emplace_back(
            input.data.data(),
            mlx::core::Shape{1, m_impl->model_resolution, m_impl->model_resolution, 4},
            mlx::core::float32, no_op);

        auto outputs = common::measure_stage(
            on_stage, "mlx_run", [&]() { return (*m_impl->function)(args); }, 1);
        if (outputs.size() < 2) {
            return Unexpected<Error>{Error{ErrorCode::InferenceFailed,
                                           "MLX bridge returned fewer than two output tensors."}};
        }

        auto alpha = mlx::core::contiguous(outputs[0]);
        auto foreground = mlx::core::contiguous(outputs[1]);

        common::measure_stage(
            on_stage, "mlx_extract_outputs",
            [&]() {
                mlx::core::eval(alpha, foreground);
                alpha.wait();
                foreground.wait();
            },
            1);

        const auto& alpha_shape = alpha.shape();
        const auto& fg_shape = foreground.shape();
        if (alpha_shape.size() != 4 || fg_shape.size() != 4 || alpha_shape[0] != 1 ||
            fg_shape[0] != 1 || alpha_shape[3] != 1 || fg_shape[3] != 3) {
            return Unexpected<Error>{
                Error{ErrorCode::InferenceFailed, "MLX bridge returned unexpected tensor shapes."}};
        }

        int output_height = static_cast<int>(alpha_shape[1]);
        int output_width = static_cast<int>(alpha_shape[2]);

        FrameResult result;
        ImageBuffer full_alpha(output_width, output_height, 1);
        ImageBuffer full_fg(output_width, output_height, 3);
        std::copy(alpha.data<float>(), alpha.data<float>() + full_alpha.view().data.size(),
                  full_alpha.view().data.begin());
        std::copy(foreground.data<float>(), foreground.data<float>() + full_fg.view().data.size(),
                  full_fg.view().data.begin());

        ImageBuffer cropped_alpha = ColorUtils::crop(full_alpha.view(), rgb_roi.x_pos,
                                                     rgb_roi.y_pos, rgb_roi.width, rgb_roi.height);
        ImageBuffer cropped_fg = ColorUtils::crop(full_fg.view(), rgb_roi.x_pos, rgb_roi.y_pos,
                                                  rgb_roi.width, rgb_roi.height);
        result.alpha = ColorUtils::resize(cropped_alpha.view(), rgb.width, rgb.height);
        result.foreground = ColorUtils::resize(cropped_fg.view(), rgb.width, rgb.height);
        return result;
    } catch (const std::exception& error) {
        return Unexpected<Error>{Error{ErrorCode::InferenceFailed, "MLX bridge execution failed: " +
                                                                       std::string(error.what())}};
    }
#endif
}

int MlxSession::model_resolution() const {
    return m_impl->model_resolution;
}

}  // namespace corridorkey::core
