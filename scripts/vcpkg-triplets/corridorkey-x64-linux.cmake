set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# corridorkey_core is a SHARED library on Linux, so every static dependency
# it links must be position-independent. Force -fPIC globally so every .a
# built through this triplet is linkable into libcorridorkey_core.so.
set(VCPKG_C_FLAGS "-fPIC")
set(VCPKG_CXX_FLAGS "-fPIC")

# ffmpeg's yasm/nasm x86 assembly modules (e.g. vc1dsp_mmx.o) still emit
# non-PIC R_X86_64_PC32 relocations even when --enable-pic is passed, which
# breaks the link of libcorridorkey_core.so. Build ffmpeg as shared objects
# for this triplet and ship the resulting libav*.so alongside the plugin.
if(PORT MATCHES "^ffmpeg$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
