#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>
#include <filesystem>
#include <functional>
#include <memory>

namespace corridorkey {

/**
 * @brief Progress callback for long-running tasks.
 * Return false to cancel the operation.
 */
using ProgressCallback = std::function<bool(float progress, const std::string& status)>;

/**
 * @brief The main inference engine for CorridorKey.
 * Implements the PIMPL pattern to hide ONNX Runtime details.
 */
class CORRIDORKEY_API Engine {
public:
    /**
     * @brief Factory method to create and initialize the engine.
     * @param model_path Path to the ONNX model file.
     * @param device The device to use for inference. Defaults to auto-detection.
     * @return A unique pointer to the initialized Engine or an error.
     */
    static Result<std::unique_ptr<Engine>> create(
        const std::filesystem::path& model_path,
        DeviceInfo device = auto_detect()
    );

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
    Result<FrameResult> process_frame(
        const Image& rgb, 
        const Image& alpha_hint,
        const InferenceParams& params = {}
    );

    /**
     * @brief Process a sequence of images from disk.
     * @param inputs List of paths to input images.
     * @param alpha_hints List of paths to alpha hint images.
     * @param output_dir Directory where results will be saved.
     * @param on_progress Optional callback for progress and cancellation.
     * @return Success or an error.
     */
    Result<void> process_sequence(
        const std::vector<std::filesystem::path>& inputs,
        const std::vector<std::filesystem::path>& alpha_hints,
        const std::filesystem::path& output_dir,
        ProgressCallback on_progress = nullptr
    );

    /**
     * @brief Get the recommended resolution based on current hardware limits.
     */
    [[nodiscard]] int recommended_resolution() const;

    /**
     * @brief Get information about the device currently in use.
     */
    [[nodiscard]] DeviceInfo current_device() const;

private:
    // Private constructor used by the factory method
    Engine();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

/**
 * @brief Detect the best available hardware device.
 */
CORRIDORKEY_API DeviceInfo auto_detect();

/**
 * @brief List all available hardware devices for inference.
 */
CORRIDORKEY_API std::vector<DeviceInfo> list_devices();

} // namespace corridorkey
