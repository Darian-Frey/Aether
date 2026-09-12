#include "core/gpu_grid.hpp"
#include "core/grid.hpp"
#include "render/renderer3d.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <raylib.h>

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

TEST_CASE("volume renderer shows a voxel at the centre and background elsewhere", "[gpu][render][3d]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{3, 16, 16, 16};
    core::HostGrid host(spec);
    host.set(8, 8, 8, 1);
    auto gpu = std::get<core::GpuGrid>(core::GpuGrid::create(spec, core::queryVram()));
    gpu.upload(host.current());

    auto made = render::Renderer3D::create();
    if (const auto* e = std::get_if<core::Error>(&made)) FAIL(e->message);
    auto& r = std::get<render::Renderer3D>(made);
    render::Palette pal = render::Palette::defaultFor(2);
    pal.entries[1] = {255, 0, 0, 255};
    r.setPalette(pal);
    r.setBackground({0, 0, 40, 255});

    render::Orbit cam;
    cam.fit(16, 16, 16);
    cam.yaw = 0.3;
    cam.pitch = 0.3;
    render::VolumeSettings vs;
    RenderTexture2D target = LoadRenderTexture(200, 200);
    BeginTextureMode(target);
    ClearBackground(BLACK);
    r.draw(gpu.current(), spec, cam, vs, Rect{0, 0, 200, 200}, 200, 200);
    EndTextureMode();
    Image img = LoadImageFromTexture(target.texture);

    // The one voxel sits at the target, so the centre pixel is red (shaded).
    const Rgba c = pixel(img, 100, 100);
    CHECK(c.r > 120);
    CHECK(c.g == 0);
    CHECK(c.b == 0);
    // Corners see through the transparent volume to the background.
    CHECK(pixel(img, 2, 2) == Rgba{0, 0, 40, 255});
    CHECK(pixel(img, 197, 197) == Rgba{0, 0, 40, 255});

    // Clip the voxel away: centre is background.
    vs.clipMax = {16, 16, 8};
    BeginTextureMode(target);
    r.draw(gpu.current(), spec, cam, vs, Rect{0, 0, 200, 200}, 200, 200);
    EndTextureMode();
    Image img2 = LoadImageFromTexture(target.texture);
    CHECK(pixel(img2, 100, 100) == Rgba{0, 0, 40, 255});

    UnloadImage(img);
    UnloadImage(img2);
    UnloadRenderTexture(target);
}

TEST_CASE("a full opaque volume is a solid shaded block and drawing leaves it untouched", "[gpu][render][3d]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{3, 8, 8, 8};
    core::HostGrid host(spec);
    for (uint8_t& c : host.current()) c = 1;
    auto gpu = std::get<core::GpuGrid>(core::GpuGrid::create(spec, core::queryVram()));
    gpu.upload(host.current());
    auto r = std::get<render::Renderer3D>(render::Renderer3D::create());
    render::Palette pal = render::Palette::defaultFor(2);
    pal.entries[1] = {200, 200, 200, 255};
    r.setPalette(pal);
    r.setBackground({0, 0, 0, 255});
    render::Orbit cam;
    cam.fit(8, 8, 8);
    RenderTexture2D target = LoadRenderTexture(120, 120);
    BeginTextureMode(target);
    r.draw(gpu.current(), spec, cam, {}, Rect{0, 0, 120, 120}, 120, 120);
    EndTextureMode();
    Image img = LoadImageFromTexture(target.texture);
    const Rgba c = pixel(img, 60, 60);
    CHECK(c.r > 100);
    CHECK(c.r == c.g);
    std::vector<uint8_t> back(spec.cellCount());
    gpu.download(back);
    CHECK(back == std::vector<uint8_t>(host.current().begin(), host.current().end()));
    UnloadImage(img);
    UnloadRenderTexture(target);
}
