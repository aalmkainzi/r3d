﻿cmake_minimum_required(VERSION 3.8)
project("r3d")

# This section configures MSVC (Visual Studio) to enable "Edit and Continue" debugging

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Determine the project's root directory and check if it's the main project

set(R3D_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(R3D_IS_MAIN ON)
else()
    set(R3D_IS_MAIN OFF)
endif()

# Project configuration options

option(R3D_BUILD_EXAMPLES "Build the examples" ${R3D_IS_MAIN})
option(R3D_RAYLIB_VENDORED "Use vendored raylib from submodule" OFF)
option(R3D_ASSIMP_VENDORED "Use vendored assimp from submodule" OFF)

# Set the cmake module path

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Include cmake scripts

include(CheckLibraryExists)
include(EmbedShaders)
include(EmbedAssets)

# Checks for assimp installation

if(R3D_ASSIMP_VENDORED)
    set(R3D_ASSIMP_SUBMODULE_PATH "${R3D_ROOT_PATH}/external/assimp")
    set(R3D_ASSIMP_CMAKELISTS "${R3D_ASSIMP_SUBMODULE_PATH}/CMakeLists.txt")

    # Vendored assimp configuration here!
    if(EXISTS "${R3D_ASSIMP_CMAKELISTS}")
        message(STATUS "Using vendored assimp from: ${R3D_ASSIMP_SUBMODULE_PATH}")

        set(ASSIMP_INJECT_DEBUG_POSTFIX     OFF CACHE BOOL "")
        set(ASSIMP_WARNINGS_AS_ERRORS       OFF CACHE BOOL "")
        set(ASSIMP_IGNORE_GIT_HASH          ON CACHE BOOL "")
        set(ASSIMP_BUILD_TESTS              OFF CACHE BOOL "")
        set(ASSIMP_NO_EXPORT                ON CACHE BOOL "")
        set(ASSIMP_INSTALL                  OFF CACHE BOOL "")

        set(ASSIMP_BUILD_COLLADA_IMPORTER   ON CACHE BOOL "")
        set(ASSIMP_BUILD_GLTF_IMPORTER      ON CACHE BOOL "")
        set(ASSIMP_BUILD_OBJ_IMPORTER       ON CACHE BOOL "")
        set(ASSIMP_BUILD_M3D_IMPORTER       ON CACHE BOOL "")
        set(ASSIMP_BUILD_FBX_IMPORTER       ON CACHE BOOL "")
        set(ASSIMP_BUILD_IQM_IMPORTER       ON CACHE BOOL "")

        set(ASSIMP_BUILD_AMF_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_BVH_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_OFF_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_COB_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_STL_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_3DS_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_AC_IMPORTER        OFF CACHE BOOL "")
        set(ASSIMP_BUILD_ASE_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_ASSBIN_IMPORTER    OFF CACHE BOOL "")
        set(ASSIMP_BUILD_B3D_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_DXF_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_CSM_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_HMP_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_IRRMESH_IMPORTER   OFF CACHE BOOL "")
        set(ASSIMP_BUILD_IRR_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_LWO_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_LWS_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_MD2_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_MD3_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_MD5_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_MDC_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_MDL_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_NFF_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_NDO_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_OGRE_IMPORTER      OFF CACHE BOOL "")
        set(ASSIMP_BUILD_OPENGEX_IMPORTER   OFF CACHE BOOL "")
        set(ASSIMP_BUILD_PLY_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_MS3D_IMPORTER      OFF CACHE BOOL "")
        set(ASSIMP_BUILD_BLEND_IMPORTER     OFF CACHE BOOL "")
        set(ASSIMP_BUILD_IFC_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_XGL_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_Q3D_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_Q3BSP_IMPORTER     OFF CACHE BOOL "")
        set(ASSIMP_BUILD_RAW_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_SIB_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_SMD_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_TERRAGEN_IMPORTER  OFF CACHE BOOL "")
        set(ASSIMP_BUILD_3D_IMPORTER        OFF CACHE BOOL "")
        set(ASSIMP_BUILD_X_IMPORTER         OFF CACHE BOOL "")
        set(ASSIMP_BUILD_X3D_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_3MF_IMPORTER       OFF CACHE BOOL "")
        set(ASSIMP_BUILD_MMD_IMPORTER       OFF CACHE BOOL "")

        add_subdirectory("${R3D_ASSIMP_SUBMODULE_PATH}")

        set(R3D_ASSIMP_INC_PATH "${R3D_ASSIMP_SUBMODULE_PATH}/src" CACHE STRING "Path to assimp includes" FORCE)
    else()
        message(FATAL_ERROR "Vendored assimp not found!
Missing: ${R3D_ASSIMP_CMAKELISTS}
Please initialize the submodule with:
    git submodule update --init --recursive")
    endif()
else()
    find_package(assimp QUIET)

    if(TARGET assimp)
        message(STATUS "Using system-installed assimp")
        get_target_property(RAYLIB_INCLUDE_DIR assimp INTERFACE_INCLUDE_DIRECTORIES)
        set(R3D_ASSIMP_INC_PATH "${RAYLIB_INCLUDE_DIR}" CACHE STRING "Path to assimp includes" FORCE)
    else()
        message(FATAL_ERROR "System assimp not found.
Make sure assimp is installed and discoverable by CMake (e.g. via CMAKE_PREFIX_PATH or pkg-config).")
    endif()
endif()

# Checks for raylib installation

# TODO: Add a version check for raylib, currently compatible with 5.6 dev
#       Do this for the next release

if(R3D_RAYLIB_VENDORED)
    set(R3D_RAYLIB_SUBMODULE_PATH "${R3D_ROOT_PATH}/external/raylib")
    set(R3D_RAYLIB_CMAKELISTS "${R3D_RAYLIB_SUBMODULE_PATH}/CMakeLists.txt")

    # Vendored assimp configuration here!
    if(EXISTS "${R3D_RAYLIB_CMAKELISTS}")
        message(STATUS "Using vendored raylib from: ${R3D_RAYLIB_SUBMODULE_PATH}")

        # Disable model loading and mesh generation support
        # to avoid symbol redefinition errors with Assimp,
        # and because it's not needed with r3d

        set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
        set(SUPPORT_FILEFORMAT_OBJ OFF CACHE BOOL "" FORCE)
        set(SUPPORT_FILEFORMAT_MTL OFF CACHE BOOL "" FORCE)
        set(SUPPORT_FILEFORMAT_IQM OFF CACHE BOOL "" FORCE)
        set(SUPPORT_FILEFORMAT_GLTF OFF CACHE BOOL "" FORCE)
        set(SUPPORT_FILEFORMAT_VOX OFF CACHE BOOL "" FORCE)
        set(SUPPORT_FILEFORMAT_M3D OFF CACHE BOOL "" FORCE)
        set(SUPPORT_MESH_GENERATION OFF CACHE BOOL "" FORCE)

        add_subdirectory("${R3D_RAYLIB_SUBMODULE_PATH}")

        set(R3D_RAYLIB_INC_PATH "${R3D_RAYLIB_SUBMODULE_PATH}/src" CACHE STRING "Path to raylib includes" FORCE)
    else()
        message(FATAL_ERROR "Vendored raylib not found!
Missing: ${R3D_RAYLIB_CMAKELISTS}
Please initialize the submodule with:
    git submodule update --init --recursive")
    endif()
else()
    find_package(raylib QUIET)

    if(TARGET raylib)
        message(STATUS "Using system-installed raylib")
        get_target_property(RAYLIB_INCLUDE_DIR raylib INTERFACE_INCLUDE_DIRECTORIES)
        set(R3D_RAYLIB_INC_PATH "${RAYLIB_INCLUDE_DIR}" CACHE STRING "Path to raylib includes" FORCE)
    else()
        message(FATAL_ERROR "System raylib not found.
Make sure raylib is installed and discoverable by CMake (e.g. via CMAKE_PREFIX_PATH or pkg-config).")
    endif()
endif()

# Create the r3d library by adding all the source files

if(BUILD_SHARED_LIBS)
    add_definitions(-DR3D_BUILD_SHARED)
    add_definitions(-DGLAD_API_CALL_EXPORT)
endif()

add_library(${PROJECT_NAME}
    "${R3D_ROOT_PATH}/src/details/r3d_primitives.c"
    "${R3D_ROOT_PATH}/src/details/r3d_billboard.c"
    "${R3D_ROOT_PATH}/src/details/r3d_drawcall.c"
    "${R3D_ROOT_PATH}/src/details/r3d_frustum.c"
    "${R3D_ROOT_PATH}/src/details/r3d_light.c"
    "${R3D_ROOT_PATH}/src/r3d_environment.c"
    "${R3D_ROOT_PATH}/src/r3d_particles.c"
    "${R3D_ROOT_PATH}/src/r3d_lighting.c"
    "${R3D_ROOT_PATH}/src/r3d_culling.c"
    "${R3D_ROOT_PATH}/src/r3d_skybox.c"
    "${R3D_ROOT_PATH}/src/r3d_curves.c"
    "${R3D_ROOT_PATH}/src/r3d_sprite.c"
    "${R3D_ROOT_PATH}/src/r3d_model.c"
    "${R3D_ROOT_PATH}/src/r3d_utils.c"
    "${R3D_ROOT_PATH}/src/r3d_state.c"
    "${R3D_ROOT_PATH}/src/r3d_core.c"
)

EmbedShaders(${PROJECT_NAME}
    "${R3D_ROOT_PATH}/shaders/common/screen.vert"
    "${R3D_ROOT_PATH}/shaders/common/cubemap.vert"
    "${R3D_ROOT_PATH}/shaders/generate/gaussian_blur_dual_pass.frag"
    "${R3D_ROOT_PATH}/shaders/generate/downsampling.frag"
    "${R3D_ROOT_PATH}/shaders/generate/upsampling.frag"
    "${R3D_ROOT_PATH}/shaders/generate/cubemap_from_equirectangular.frag"
    "${R3D_ROOT_PATH}/shaders/generate/irradiance_convolution.frag"
    "${R3D_ROOT_PATH}/shaders/generate/prefilter.frag"
    "${R3D_ROOT_PATH}/shaders/raster/geometry.vert"
    "${R3D_ROOT_PATH}/shaders/raster/geometry_instanced.vert"
    "${R3D_ROOT_PATH}/shaders/raster/geometry.frag"
    "${R3D_ROOT_PATH}/shaders/raster/forward.vert"
    "${R3D_ROOT_PATH}/shaders/raster/forward_instanced.vert"
    "${R3D_ROOT_PATH}/shaders/raster/forward.frag"
    "${R3D_ROOT_PATH}/shaders/raster/skybox.vert"
    "${R3D_ROOT_PATH}/shaders/raster/skybox.frag"
    "${R3D_ROOT_PATH}/shaders/raster/depth_volume.vert"
    "${R3D_ROOT_PATH}/shaders/raster/depth_volume.frag"
    "${R3D_ROOT_PATH}/shaders/raster/depth.vert"
    "${R3D_ROOT_PATH}/shaders/raster/depth_instanced.vert"
    "${R3D_ROOT_PATH}/shaders/raster/depth.frag"
    "${R3D_ROOT_PATH}/shaders/raster/depth_cube.vert"
    "${R3D_ROOT_PATH}/shaders/raster/depth_cube_instanced.vert"
    "${R3D_ROOT_PATH}/shaders/raster/depth_cube.frag"
    "${R3D_ROOT_PATH}/shaders/screen/ssao.frag"
    "${R3D_ROOT_PATH}/shaders/screen/ambient.frag"
    "${R3D_ROOT_PATH}/shaders/screen/lighting.frag"
    "${R3D_ROOT_PATH}/shaders/screen/scene.frag"
    "${R3D_ROOT_PATH}/shaders/screen/bloom.frag"
    "${R3D_ROOT_PATH}/shaders/screen/fog.frag"
    "${R3D_ROOT_PATH}/shaders/screen/output.frag"
    "${R3D_ROOT_PATH}/shaders/screen/fxaa.frag"
)

EmbedAssets(${PROJECT_NAME}
    "${R3D_ROOT_PATH}/assets/lut/ibl_brdf_256.dds"
    "${R3D_ROOT_PATH}/assets/noise/blue_noise_128.png"
)

# Link project dependencies

target_link_libraries(${PROJECT_NAME} PUBLIC raylib assimp)

check_library_exists(m cos "" HAVE_LIB_M)

if(HAVE_LIB_M)
    target_link_libraries(${PROJECT_NAME} PUBLIC m)
endif()

# Specify the include directories needed for the project

target_include_directories(${PROJECT_NAME} PUBLIC
    "${R3D_ROOT_PATH}/external/glad"
    "${R3D_ROOT_PATH}/include"
    "${R3D_RAYLIB_INC_PATH}"
    "${R3D_ASSIMP_INC_PATH}"
)

# Ensure the C standard is set for MSVC if using CMake version greater than 3.12

if(CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET r3d PROPERTY C_STANDARD 99)
endif()

# Example configuration

if(R3D_BUILD_EXAMPLES)
    include("${R3D_ROOT_PATH}/examples/CMakeLists.txt")
endif()
