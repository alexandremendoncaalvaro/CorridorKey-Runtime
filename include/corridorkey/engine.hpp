#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>
#include <filesystem>
#include <functional>
#include <memory>

namespace corridorkey {

namespace core {
struct EngineFactory;
}

/**
 * @brief Detect the best available hardware device.
 */
CORRIDORKEY_API DeviceInfo auto_detect();

/**
 * @brief List all available hardware devices for inference.
 */
CORRIDORKEY_API std::vector<DeviceInfo> list_devices();

/**
 * @brief Report runtime capabilities needed by diagnostics and future GUI bridges.
 */
CORRIDORKEY_API RuntimeCapabilities runtime_capabilities();

/**
 * @brief Built-in model catalog shared across CLI and GUI surfaces.
 */
CORRIDORKEY_API std::vector<ModelCatalogEntry> model_catalog();

/**
 * @brief Built-in processing presets shared across CLI and GUI surfaces.
 */
CORRIDORKEY_API std::vector<PresetDefinition> preset_catalog();

/**
 * @brief Progress callback for long-running tasks.
 * @param progress Fraction in [0.0, 1.0], or negative when the total work is
 *                 unknown (indeterminate progress — renderer should show a
 *                 spinner rather than a partial bar).
 * @return false to cancel the operation.
 */
using ProgressCallback = std::function<bool(float progress, const std::string& status)>;

/**
 * @brief Structured event callback for NDJSON output and future GUI bridges.
 * Return false to cancel the operation.
 */
using JobEventCallback = std::function<bool(const JobEvent& event)>;

/**
 * @brief Controls how the engine handles backend fallback during session creation and execution.
 */
struct EngineCreateOptions {
    bool allow_cpu_fallback = true;
    bool disable_cpu_ep_fallback = false;
};

/**
 * @brief The main inference engine for CorridorKey.
 * Implements the PIMPL pattern to hide ONNX Runtime details.
 */
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
class CORRIDORKEY_API Engine {
   public:
    /**
     * @brief Factory method to create and initialize the engine.
     * @param model_path Path to the ONNX model file.
     * @param device The device to use for inference. Defaults to auto-detection.
     * @param on_stage Optional callback for stage timing diagnostics.
     * @param options Controls CPU fallback policy for interactive vs. tolerant workflows.
     * @return A unique pointer to the initialized Engine or an error.
     */
    static Result<std::unique_ptr<Engine>> create(const std::filesystem::path& model_path,
                                                  DeviceInfo device = auto_detect(),
                                                  StageTimingCallback on_stage = nullptr,
                                                  EngineCreateOptions options = {});

    /**
     * @brief Destructor (virtual for safety, though Engine is usually not inherited).
     */
    virtual ~Engine();

    // Disable copy, allow move
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    /**
     * @brief Process a single RGB frame with an alpha hint.
     * @param rgb The input RGB image (straight color).
     * @param alpha_hint The alpha hint image (1-channel or 3-channel).
     * @param params Inference and post-processing parameters.
     * @return The resulting images or an error.
     */
    Result<FrameResult> process_frame(const Image& rgb, const Image& alpha_hint,
                                      const InferenceParams& params = {},
                                      StageTimingCallback on_stage = nullptr);

    /**
     * @brief Process a batch of RGB frames with alpha hints.
     * @param rgbs List of input RGB images.
     * @param alpha_hints List of alpha hint images.
     * @param params Inference and post-processing parameters.
     * @return List of results or an error.
     */
    Result<std::vector<FrameResult>> process_frame_batch(const std::vector<Image>& rgbs,
                                                         const std::vector<Image>& alpha_hints,
                                                         const InferenceParams& params = {},
                                                         StageTimingCallback on_stage = nullptr);

    /**
     * @brief Process a sequence of images from disk.
     * @param inputs List of paths to input images.
     * @param alpha_hints List of paths to alpha hint images.
     * @param output_dir Directory where results will be saved.
     * @param params Inference and post-processing parameters.
     * @param on_progress Optional callback for progress and cancellation.
     * @return Success or an error.
     */
    Result<void> process_sequence(const std::vector<std::filesystem::path>& inputs,
                                  const std::vector<std::filesystem::path>& alpha_hints,
                                  const std::filesystem::path& output_dir,
                                  const InferenceParams& params = {},
                                  ProgressCallback on_progress = nullptr,
                                  StageTimingCallback on_stage = nullptr);

    /**
     * @brief Process a video file directly using FFmpeg.
     * @param input_video Path to input video file.
     * @param hint_video Path to alpha hint video file.
     * @param output_video Path where the resulting video will be saved.
     * @param params Inference and post-processing parameters.
     * @param on_progress Optional callback for progress and cancellation.
     * @return Success or an error.
     */
    Result<void> process_video(const std::filesystem::path& input_video,
                               const std::filesystem::path& hint_video,
                               const std::filesystem::path& output_video,
                               const InferenceParams& params = {},
                               ProgressCallback on_progress = nullptr,
                               StageTimingCallback on_stage = nullptr);
    /**
     * @brief Process a video file directly using FFmpeg with explicit output options.
     * @param input_video Path to input video file.
     * @param hint_video Path to alpha hint video file.
     * @param output_video Path where the resulting video will be saved.
     * @param params Inference and post-processing parameters.
     * @param output_options Video output encoding policy and container preferences.
     * @param on_progress Optional callback for progress and cancellation.
     * @return Success or an error.
     */
    Result<void> process_video(const std::filesystem::path& input_video,
                               const std::filesystem::path& hint_video,
                               const std::filesystem::path& output_video,
                               const InferenceParams& params,
                               const VideoOutputOptions& output_options,
                               ProgressCallback on_progress = nullptr,
                               StageTimingCallback on_stage = nullptr);

    /**
     * @brief Get the recommended resolution based on current hardware limits.
     * Use this as a default value if InferenceParams::target_resolution is 0.
     */
    [[nodiscard]] int recommended_resolution() const;

    /**
     * @brief Get information about the device currently in use.
     */
    [[nodiscard]] DeviceInfo current_device() const;

    /**
     * @brief Get information about the last backend fallback, if one happened.
     */
    [[nodiscard]] std::optional<BackendFallbackInfo> backend_fallback() const;

   private:
    // Private constructor used by the factory method
    Engine();
    friend struct core::EngineFactory;

    class Impl;
    std::unique_ptr<Impl> m_impl;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

}  // namespace corridorkey
