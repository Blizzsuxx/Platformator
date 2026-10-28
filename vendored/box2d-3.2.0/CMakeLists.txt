# Platformator's dynamic-tree-only build. Upstream source files are unmodified.
add_library(platformator_box2d_tree OBJECT
    src/dynamic_tree.c
    src/aabb.c
    src/core.c
    src/math_functions.c
)
set_target_properties(platformator_box2d_tree PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(platformator_box2d_tree PRIVATE include src)
target_compile_definitions(platformator_box2d_tree PRIVATE
    "$<$<CONFIG:Debug>:BOX2D_VALIDATE>"
    "$<$<CONFIG:RelWithDebInfo>:B2_ENABLE_ASSERT>"
)
# Match upstream's floating-point contraction policy without changing engine flags.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(platformator_box2d_tree PRIVATE /clang:-ffp-contract=off)
    else()
        target_compile_options(platformator_box2d_tree PRIVATE -ffp-contract=off)
    endif()
endif()
if(EMSCRIPTEN)
    target_compile_options(platformator_box2d_tree PRIVATE -msimd128 -msse2)
endif()
