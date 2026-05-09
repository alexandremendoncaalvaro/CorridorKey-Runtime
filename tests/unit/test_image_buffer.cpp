#include <corridorkey/types.hpp>

#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#include <malloc.h>
#else
#include <cstdlib>
#endif

using namespace corridorkey;

float* allocate_test_storage(std::size_t count) {
#ifdef _WIN32
    return static_cast<float*>(_aligned_malloc(count * sizeof(float), MEMORY_ALIGNMENT));
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, MEMORY_ALIGNMENT, count * sizeof(float)) != 0) {
        return nullptr;
    }
    return static_cast<float*>(ptr);
#endif
}

void free_test_storage(float* ptr) {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

TEST_CASE("ImageBuffer can adopt externally owned aligned storage", "[unit][image-buffer]") {
    auto* data = allocate_test_storage(6);
    REQUIRE(data != nullptr);
    bool freed = false;

    {
        ImageBuffer buffer = ImageBuffer::adopt(2, 1, 3, data, [&](float* ptr) {
            freed = true;
            free_test_storage(ptr);
        });
        auto view = buffer.view();
        REQUIRE(view.width == 2);
        REQUIRE(view.height == 1);
        REQUIRE(view.channels == 3);
        REQUIRE(view.data.data() == data);
        view.data[5] = 0.75F;
        CHECK(data[5] == 0.75F);
    }

    CHECK(freed);
}

TEST_CASE("ImageBuffer move preserves adopted storage ownership", "[unit][image-buffer]") {
    auto* data = allocate_test_storage(4);
    REQUIRE(data != nullptr);
    int free_count = 0;

    {
        ImageBuffer first = ImageBuffer::adopt(2, 2, 1, data, [&](float* ptr) {
            ++free_count;
            free_test_storage(ptr);
        });
        ImageBuffer second = std::move(first);
        CHECK(second.view().data.data() == data);
    }

    CHECK(free_count == 1);
}
