# Application layer implementation
set(APP_SOURCES
    job_orchestrator.cpp
    model_compiler.cpp
    ofx_runtime_protocol.cpp
    ofx_runtime_service.cpp
    ofx_session_broker.cpp
    runtime_diagnostics.cpp
    runtime_contracts.cpp
    version_check.cpp
)

target_sources(corridorkey_core PRIVATE ${APP_SOURCES})
target_link_libraries(corridorkey_core PRIVATE cpr::cpr)

# corridorkey_core_static is defined later in src/CMakeLists.txt and inherits
# sources via get_target_property, so we do not wire it up here.
