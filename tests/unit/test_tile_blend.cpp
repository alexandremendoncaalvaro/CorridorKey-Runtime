#include <catch2/catch_all.hpp>

#include "core/tile_blend.hpp"

using namespace corridorkey;

TEST_CASE("Tile blend fades only across internal seams", "[unit][tiling][regression]") {
    constexpr int tile_size = 512;
    constexpr int overlap = 64;

    SECTION("Tile overlap matches Python MLX semantics") {
        CHECK(core::tile_stride(512, 64) == 448);
        CHECK(core::tile_stride(1024, 64) == 960);
    }

    SECTION("Interior tile keeps seam fade on both sides") {
        CHECK(core::edge_aware_tile_weight(0, 256, tile_size, overlap, false, false, false,
                                           false) == Catch::Approx(0.0F));
        CHECK(core::edge_aware_tile_weight(32, 256, tile_size, overlap, false, false, false,
                                           false) == Catch::Approx(0.5F));
        CHECK(core::edge_aware_tile_weight(64, 256, tile_size, overlap, false, false, false,
                                           false) == Catch::Approx(1.0F));
    }

    SECTION("Left frame edge stays fully weighted") {
        CHECK(core::edge_aware_tile_weight(0, 256, tile_size, overlap, true, false, false, false) ==
              Catch::Approx(1.0F));
        CHECK(core::edge_aware_tile_weight(32, 256, tile_size, overlap, true, false, false,
                                           false) == Catch::Approx(1.0F));
    }

    SECTION("Top-left frame corner stays fully weighted") {
        CHECK(core::edge_aware_tile_weight(0, 0, tile_size, overlap, true, false, true, false) ==
              Catch::Approx(1.0F));
    }

    SECTION("Boundary tile still fades toward the next internal seam") {
        CHECK(core::edge_aware_tile_weight(511, 256, tile_size, overlap, true, false, false,
                                           false) == Catch::Approx(0.0F));
        CHECK(core::edge_aware_tile_weight(479, 256, tile_size, overlap, true, false, false,
                                           false) == Catch::Approx(0.5F));
    }
}
