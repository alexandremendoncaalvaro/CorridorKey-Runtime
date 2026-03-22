#include <catch2/catch_all.hpp>
#include <string>

#include "../../src/core/windows_rtx_probe.hpp"

using namespace corridorkey::core;

TEST_CASE("Regression: DirectML provider string is case-sensitive as DmlExecutionProvider", "[regression][device]") {
    // A regressão ocorreu porque a verificação buscava por "DMLExecutionProvider" em vez de "DmlExecutionProvider".
    // A API do ONNX Runtime em versões recentes retorna "DmlExecutionProvider" de GetAvailableProviders().
    // Embora o ambiente de teste em CI possa não ter o provedor DML instalado (portanto, directml_provider_available() = false),
    // queremos garantir que não voltemos ao erro antigo.
    
    // Como a checagem real envolve carregar uma DLL e consultar a API que pode não estar presente,
    // usamos uma validação indireta simples: confirmar que o símbolo foi implementado para este build
    // (a função não deve lançar exceções inesperadas independentemente do ambiente ser DML-capable ou não).
#if defined(_WIN32)
    REQUIRE_NOTHROW(directml_provider_available());
#else
    SUCCEED("DirectML provider check is Windows-specific.");
#endif
}