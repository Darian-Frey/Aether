#include "core/gpu_grid.hpp"
#include "core/grid.hpp"
#include "render/renderer2d.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <raylib.h>

#include <vector>

using namespace aether;
using aether::test::GlContext;
using aether::test::requireGl;
using render::Rect;
using render::Rgba;

namespace {

Rgba pixel(const Image& img, int x, int yTopDown) {
    const Color c = GetImageColor(img, x, img.height - 1 - yTopDown);
    return {c.r, c.g, c.b, c.a};
}

}  // namespace

TEST_CASE("renderer maps cells to palette colours pixel-exactly at integer zoom", "[gpu][render]") {
    GlContext gl;
    requireGl(gl);

    const core::GridSpec spec{2, 4, 3, 1};
    core::HostGrid host(spec);
    host.set(0, 0, 0, 1);
    host.set(3, 0, 0, 2);
    host.set(1, 2, 0, 3);
    auto made = core::GpuGrid::create(spec, core::queryVram());
    REQUIRE(std::holds_alternative<core::GpuGrid>(made));
    auto& gpu = std::get<core::GpuGrid>(made);
    gpu.upload(host.current());

    auto r = render::Renderer2D::create();
    if (const auto* e = std::get_if<core::Error>(&r)) FAIL(e->message);
    auto& renderer = std::get<render::Renderer2D>(r);
    render::Palette pal;
    pal.entries[0] = {10, 20, 30, 255};
    pal.entries[1] = {255, 0, 0, 255};
    pal.entries[2] = {0, 255, 0, 255};
    pal.entries[3] = {0, 0, 255, 255};
    renderer.setPalette(pal);
    renderer.setBackground({7, 7, 7, 255});

    // Zoom 2 into a 12x10 target: the 8x6 grid sits at pixel (2,2).
    render::View2D view;
    view.zoom = 2.0;
    view.centre_x = 2.0;
    view.centre_y = 1.5;
    RenderTexture2D target = LoadRenderTexture(12, 10);
    BeginTextureMode(target);
    ClearBackground(BLACK);
    renderer.draw(gpu.current(), spec, view, Rect{0, 0, 12, 10}, 12, 10, 4);
    EndTextureMode();
    Image img = LoadImageFromTexture(target.texture);

    CHECK(pixel(img, 0, 0) == Rgba{7, 7, 7, 255});     // outside the grid: background
    CHECK(pixel(img, 11, 9) == Rgba{7, 7, 7, 255});
    CHECK(pixel(img, 2, 2) == Rgba{255, 0, 0, 255});   // cell (0,0)
    CHECK(pixel(img, 3, 3) == Rgba{255, 0, 0, 255});   // still cell (0,0) at zoom 2
    CHECK(pixel(img, 4, 2) == Rgba{10, 20, 30, 255});  // cell (1,0): state 0
    CHECK(pixel(img, 8, 2) == Rgba{0, 255, 0, 255});   // cell (3,0)
    CHECK(pixel(img, 9, 3) == Rgba{0, 255, 0, 255});
    CHECK(pixel(img, 4, 6) == Rgba{0, 0, 255, 255});   // cell (1,2)
    CHECK(pixel(img, 5, 7) == Rgba{0, 0, 255, 255});
    CHECK(pixel(img, 6, 6) == Rgba{10, 20, 30, 255});  // cell (2,2)

    UnloadImage(img);
    UnloadRenderTexture(target);
}

TEST_CASE("drawing does not alter the state texture", "[gpu][render]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 16, 16, 1};
    core::HostGrid host(spec);
    for (uint32_t i = 0; i < 16; ++i) host.set(i, i, 0, 1);
    auto gpu = std::get<core::GpuGrid>(core::GpuGrid::create(spec, core::queryVram()));
    gpu.upload(host.current());
    auto renderer = std::get<render::Renderer2D>(render::Renderer2D::create());
    render::View2D view;
    view.fit(16, 16, Rect{0, 0, 64, 64});
    RenderTexture2D target = LoadRenderTexture(64, 64);
    for (int i = 0; i < 5; ++i) {
        BeginTextureMode(target);
        renderer.draw(gpu.current(), spec, view, Rect{0, 0, 64, 64}, 64, 64, 2);
        EndTextureMode();
    }
    std::vector<uint8_t> back(spec.cellCount());
    gpu.download(back);
    CHECK(back == std::vector<uint8_t>(host.current().begin(), host.current().end()));
    UnloadRenderTexture(target);
}

TEST_CASE("hex rendering colours each hex from its axial cell", "[gpu][render][hex]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 5, 4, 1};
    core::HostGrid host(spec);
    // A distinct state per cell so any mis-mapping shows.
    for (uint32_t y = 0; y < 4; ++y) for (uint32_t x = 0; x < 5; ++x) host.set(x, y, 0, static_cast<uint8_t>(1 + y * 5 + x));
    auto gpu = std::get<core::GpuGrid>(core::GpuGrid::create(spec, core::queryVram()));
    gpu.upload(host.current());
    auto renderer = std::get<render::Renderer2D>(render::Renderer2D::create());
    render::Palette pal;
    for (int s = 0; s < 256; ++s) pal.entries[static_cast<size_t>(s)] = {static_cast<uint8_t>(s), static_cast<uint8_t>(255 - s), 7, 255};
    renderer.setPalette(pal);
    renderer.setBackground({0, 0, 0, 255});

    render::View2D view;
    view.lattice = render::Lattice::Hex;
    view.fit(5, 4, Rect{0, 0, 160, 120});
    RenderTexture2D target = LoadRenderTexture(160, 120);
    BeginTextureMode(target);
    ClearBackground(BLACK);
    renderer.draw(gpu.current(), spec, view, Rect{0, 0, 160, 120}, 160, 120, 21);
    EndTextureMode();
    Image img = LoadImageFromTexture(target.texture);

    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 5; ++x) {
            const auto [sx, sy] = view.cellToScreen(x, y, Rect{0, 0, 160, 120});
            const Rgba p = pixel(img, static_cast<int>(sx), static_cast<int>(sy));
            CHECK(p.r == 1 + y * 5 + x);
            // The point a third of the way toward the (1,0) neighbour is the same hex.
            const Rgba q = pixel(img, static_cast<int>(sx + view.zoom / 3.0), static_cast<int>(sy));
            CHECK(q.r == 1 + y * 5 + x);
        }
    }
    // Far outside the rhombus: background.
    CHECK(pixel(img, 1, 118) == Rgba{0, 0, 0, 255});
    UnloadImage(img);
    UnloadRenderTexture(target);
}
