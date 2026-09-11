#include "core/gpu_grid.hpp"
#include "core/gl.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <vector>

using namespace aether::core;
using aether::test::GlContext;
using aether::test::requireGl;

TEST_CASE("GpuGrid round-trips a 2D grid through the current texture", "[core][gpu]") {
    GlContext gl;
    requireGl(gl);

    const GridSpec spec{2, 37, 11, 1};   // deliberately not a multiple of 4
    auto made = GpuGrid::create(spec, queryVram());
    REQUIRE(std::holds_alternative<GpuGrid>(made));
    GpuGrid& g = std::get<GpuGrid>(made);
    CHECK(g.target() == GL_TEXTURE_2D);
    CHECK(g.format() == GL_R8UI);
    CHECK(g.current() != g.next());

    std::vector<uint8_t> in(spec.cellCount());
    for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<uint8_t>(i * 7 % 251);
    g.upload(in);

    std::vector<uint8_t> out(spec.cellCount(), 0xff);
    g.download(out);
    CHECK(out == in);

    // next() is a different texture and was zeroed at creation.
    g.swap();
    g.download(out);
    CHECK(std::accumulate(out.begin(), out.end(), 0u) == 0u);
}

TEST_CASE("GpuGrid round-trips a 3D grid", "[core][gpu]") {
    GlContext gl;
    requireGl(gl);

    const GridSpec spec{3, 9, 7, 5};
    auto made = GpuGrid::create(spec, queryVram());
    REQUIRE(std::holds_alternative<GpuGrid>(made));
    GpuGrid& g = std::get<GpuGrid>(made);
    CHECK(g.target() == GL_TEXTURE_3D);

    std::vector<uint8_t> in(spec.cellCount());
    for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<uint8_t>(i % 5);
    g.upload(in);
    std::vector<uint8_t> out(spec.cellCount(), 0xff);
    g.download(out);
    CHECK(out == in);
}

TEST_CASE("GpuGrid refuses a grid the VRAM guard rejects, allocating nothing", "[core][gpu]") {
    GlContext gl;
    requireGl(gl);

    VramInfo tiny;
    tiny.source = "test";
    tiny.available_bytes = 1024;
    auto made = GpuGrid::create(GridSpec{2, 256, 256, 1}, tiny);
    REQUIRE(std::holds_alternative<Error>(made));
    CHECK(std::get<Error>(made).message.find("VRAM") != std::string::npos);
}

TEST_CASE("GpuGrid refuses extents beyond the texture limit", "[core][gpu]") {
    GlContext gl;
    requireGl(gl);

    auto made = GpuGrid::create(GridSpec{2, 1u << 20, 1, 1}, VramInfo{});
    REQUIRE(std::holds_alternative<Error>(made));
    CHECK(std::get<Error>(made).message.find("texture limit") != std::string::npos);
}

TEST_CASE("queryVram reports a source and, on NVIDIA, a figure", "[core][gpu]") {
    GlContext gl;
    requireGl(gl);

    const VramInfo v = queryVram();
    CHECK_FALSE(v.source.empty());
    if (v.source == "NVX") CHECK(v.available_bytes.value_or(0) > 0);
}
