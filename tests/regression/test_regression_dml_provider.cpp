#include <catch2/catch_all.hpp>
#include <corridorkey/detail/constants.hpp>
#include <corridorkey/engine.hpp>
#include <string>
#include <algorithm>

#if __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#endif

using namespace corridorkey::detail;

TEST_CASE("Regression: DirectML provider string matches ONNX Runtime expectations", "[regression][device]") {
    // A regressão ocorreu porque a verificação buscava por "DMLExecutionProvider" em vez de "DmlExecutionProvider".
    // Este teste garante que nossa constante centralizada condiz com o que o ONNX Runtime espera.
    
    REQUIRE(providers::DIRECTML == "DmlExecutionProvider");

#if defined(_WIN32)
    // Se estivermos em um ambiente com ONNX Runtime carregado, validamos se o provider 
    // reportado (caso exista) usa exatamente essa grafia.
    try {
        Ort::Env env;
        auto available = Ort::GetAvailableProviders();
        
        bool found_dml_with_wrong_case = false;
        for (const auto& p : available) {
            std::string p_upper = p;
            std::transform(p_upper.begin(), p_upper.end(), p_upper.begin(), ::toupper);
            
            if (p_upper == "DMLEXECUTIONPROVIDER" && p != providers::DIRECTML) {
                found_dml_with_wrong_case = true;
            }
        }
        
        // Se achamos algo que parece DML mas não é nossa string, falhamos.
        REQUIRE_FALSE(found_dml_with_wrong_case);
        
    } catch (...) {
        // Se não houver ORT no ambiente de teste, não falhamos o teste de unidade,
        // pois a validação de "magic string" acima já protege a regressão principal.
        SUCCEED("ONNX Runtime not available for dynamic provider check.");
    }
#endif
}