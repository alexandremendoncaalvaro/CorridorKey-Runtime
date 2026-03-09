#include <corridorkey/engine.hpp>
#include <corridorkey/version.hpp>
#include <cxxopts.hpp>
#include <iostream>
#include <filesystem>
#include <iomanip>

using namespace corridorkey;

void print_info() {
    auto devices = list_devices();
    std::cout << "CorridorKey Runtime v" << CORRIDORKEY_VERSION_STRING << "\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Detected Hardware Devices:\n";
    for (const auto& d : devices) {
        std::string backend_name;
        switch (d.backend) {
            case Backend::CPU: backend_name = "CPU (Universal)"; break;
            case Backend::CoreML: backend_name = "CoreML (Apple Silicon NPU/GPU)"; break;
            case Backend::TensorRT: backend_name = "TensorRT (NVIDIA RTX)"; break;
            case Backend::CUDA: backend_name = "CUDA (NVIDIA)"; break;
            case Backend::DirectML: backend_name = "DirectML (Windows GPU)"; break;
            default: backend_name = "Unknown"; break;
        }
        std::cout << " - " << std::left << std::setw(30) << d.name 
                  << " [" << backend_name << "] " 
                  << (d.available_memory_mb > 0 ? (std::to_string(d.available_memory_mb) + " MB") : "") << "\n";
    }
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("corridorkey", "High-performance neural green screen keyer runtime");

    options.add_options()
        ("command", "Sub-command to run (info, process)", cxxopts::value<std::string>())
        ("i,input", "Input video or directory of frames", cxxopts::value<std::string>())
        ("a,alpha-hint", "Alpha hint video or directory", cxxopts::value<std::string>())
        ("o,output", "Output directory", cxxopts::value<std::string>())
        ("m,model", "Path to GreenFormer ONNX model", cxxopts::value<std::string>())
        ("r,resolution", "Target resolution (0=auto, 512, 1024, etc.)", cxxopts::value<int>()->default_value("0"))
        ("d,device", "Device index from 'info' command", cxxopts::value<int>()->default_value("0"))
        ("v,version", "Print version")
        ("h,help", "Print help");

    options.parse_positional({"command"});

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (result.count("version")) {
            std::cout << "CorridorKey Runtime v" << CORRIDORKEY_VERSION_STRING << std::endl;
            return 0;
        }

        if (!result.count("command")) {
            std::cout << "Error: No command specified. Use 'info' or 'process'." << std::endl;
            return 1;
        }

        std::string cmd = result["command"].as<std::string>();

        if (cmd == "info") {
            print_info();
            return 0;
        } 

        if (cmd == "download") {
            std::string url = "https://example.com/models/GreenFormer.onnx"; // Placeholder URL
            std::string dest = "models/GreenFormer.onnx";
            std::cout << "Downloading model from " << url << "..." << std::endl;
            std::string cmd_str = "curl -L " + url + " -o " + dest;
            int ret = system(cmd_str.c_str());
            if (ret == 0) {
                std::cout << "Model downloaded successfully to " << dest << std::endl;
            } else {
                std::cerr << "Error downloading model." << std::endl;
            }
            return ret;
        }
        
        if (cmd == "process") {
            if (!result.count("input") || !result.count("alpha-hint") || !result.count("output") || !result.count("model")) {
                std::cout << "Error: 'process' command requires --input, --alpha-hint, --output, and --model." << std::endl;
                return 1;
            }

            std::filesystem::path input_path = result["input"].as<std::string>();
            std::filesystem::path hint_path = result["alpha-hint"].as<std::string>();
            std::filesystem::path output_path = result["output"].as<std::string>();
            std::filesystem::path model_path = result["model"].as<std::string>();

            auto devices = list_devices();
            int device_idx = result["device"].as<int>();
            auto engine_res = Engine::create(model_path, devices[device_idx]);
            if (!engine_res) {
                std::cerr << "Engine Error: " << engine_res.error().message << std::endl;
                return 1;
            }
            auto engine = std::move(*engine_res);

            std::vector<std::filesystem::path> inputs;
            std::vector<std::filesystem::path> hints;

            if (std::filesystem::is_directory(input_path)) {
                for (const auto& entry : std::filesystem::directory_iterator(input_path)) {
                    if (entry.is_regular_file()) {
                        inputs.push_back(entry.path());
                        // Assume hints have same filename in hint directory
                        hints.push_back(hint_path / entry.path().filename());
                    }
                }
                std::sort(inputs.begin(), inputs.end());
                std::sort(hints.begin(), hints.end());
            } else {
                inputs.push_back(input_path);
                hints.push_back(hint_path);
            }

            std::cout << "Processing " << inputs.size() << " frames..." << std::endl;
            
            auto progress = [](float p, const std::string& status) -> bool {
                int bar_width = 50;
                std::cout << "\r[" << std::string(bar_width * p, '=') << std::string(bar_width * (1-p), ' ') << "] " 
                          << int(p * 100.0) << "% " << status << std::flush;
                return true;
            };

            auto process_res = engine->process_sequence(inputs, hints, output_path, progress);
            std::cout << std::endl;

            if (!process_res) {
                std::cerr << "Error: " << process_res.error().message << std::endl;
                return 1;
            }

            std::cout << "Done!" << std::endl;
            return 0;
        }

        std::cout << "Unknown command: " << cmd << std::endl;
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error parsing arguments: " << e.what() << std::endl;
        return 1;
    }
}
