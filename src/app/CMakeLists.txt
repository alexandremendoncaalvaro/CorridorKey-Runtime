# Application layer implementation. Split between corridorkey_common (ORT-free
# pieces the .ofx needs) and corridorkey_core (rest of the orchestration that
# brings ONNX Runtime in transitively).

# Files that do not reference ONNX Runtime directly or transitively. These go
# in corridorkey_common so the .ofx can call into them without dragging
# onnxruntime.dll into its import table.
set(APP_COMMON_SOURCES
    ofx_runtime_protocol.cpp
    runtime_contracts.cpp
    version_check.cpp
)
target_sources(corridorkey_common PRIVATE ${APP_COMMON_SOURCES})

# Files that orchestrate the inference engine and the OFX runtime service
# (both pull in src/core/ ORT consumers). These stay in corridorkey_core and
# are only reached through corridorkey_ofx_runtime_server.exe / the CLI.
set(APP_CORE_SOURCES
    job_orchestrator.cpp
    model_compiler.cpp
    ofx_runtime_service.cpp
    ofx_session_broker.cpp
    runtime_contracts_probes.cpp
    runtime_diagnostics.cpp
)
target_sources(corridorkey_core PRIVATE ${APP_CORE_SOURCES})

# corridorkey_core_static is defined later in src/CMakeLists.txt and inherits
# sources via get_target_property, so we do not wire it up here.
