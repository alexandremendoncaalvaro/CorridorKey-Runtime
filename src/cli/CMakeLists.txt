# CLI implementation
add_executable(corridorkey_cli main.cpp)

# Alias for consistent naming
set_target_properties(corridorkey_cli PROPERTIES OUTPUT_NAME corridorkey)

# Link against the core library
target_link_libraries(corridorkey_cli
    PRIVATE
        corridorkey_core
        cxxopts::cxxopts
)
