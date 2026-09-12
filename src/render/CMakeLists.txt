aether_embed_shader(palette2d.vert kPalette2dVert PALETTE_VERT_CPP)
aether_embed_shader(palette2d.frag kPalette2dFrag PALETTE_FRAG_CPP)
aether_embed_shader(volume.frag kVolumeFrag VOLUME_FRAG_CPP)

add_library(aether_render STATIC
    palette.cpp
    renderer2d.cpp
    renderer3d.cpp
    ${PALETTE_VERT_CPP}
    ${PALETTE_FRAG_CPP}
    ${VOLUME_FRAG_CPP})

target_include_directories(aether_render PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(aether_render PUBLIC cxx_std_20)
target_compile_options(aether_render PRIVATE ${AETHER_WARNINGS})
target_link_libraries(aether_render PUBLIC aether_core raylib)
