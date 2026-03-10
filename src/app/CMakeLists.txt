# Application layer implementation
set(APP_SOURCES
    job_orchestrator.cpp
    runtime_diagnostics.cpp
    runtime_contracts.cpp
)

target_sources(corridorkey_core PRIVATE ${APP_SOURCES})

if(TARGET corridorkey_core_static)
    target_sources(corridorkey_core_static PRIVATE ${APP_SOURCES})
endif()
