# Application layer implementation
set(APP_SOURCES
    job_orchestrator.cpp
    model_compiler.cpp
    ofx_runtime_protocol.cpp
    ofx_runtime_service.cpp
    ofx_session_broker.cpp
    runtime_diagnostics.cpp
    runtime_contracts.cpp
)

target_sources(corridorkey_core PRIVATE ${APP_SOURCES})

if(TARGET corridorkey_core_static)
    target_sources(corridorkey_core_static PRIVATE ${APP_SOURCES})
endif()
