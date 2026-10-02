cmake_minimum_required(VERSION 3.25)

project(ReZeroGui
    VERSION 0.1.0
    DESCRIPTION "A lightweight immediate mode GUI library for embedders"
    LANGUAGES CXX
)

option(REZEROGUI_BUILD_EXAMPLES "Build the ReZeroGui examples" ON)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(REZEROGUI_NO_EXCEPTIONS -fno-exceptions -fno-unwind-tables)
    set(REZEROGUI_NO_RTTI -fno-rtti)
    set(REZEROGUI_WARNING_OPTIONS -Wall -Wextra)
elseif(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(REZEROGUI_NO_EXCEPTIONS /EHs- /EHc- /D_HAS_EXCEPTIONS=0)
    set(REZEROGUI_NO_RTTI /GR-)
    set(REZEROGUI_WARNING_OPTIONS /W4 /utf-8)
endif()

add_library(ReZeroGui INTERFACE)
add_library(ReZero::ReZeroGui ALIAS ReZeroGui)

add_subdirectory(ReZeroTrueType2)

target_compile_features(ReZeroGui INTERFACE cxx_std_23)

target_include_directories(ReZeroGui INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(ReZeroGui INTERFACE ReZero::ReZeroTrueType)

target_compile_options(ReZeroGui INTERFACE 
    ${REZEROGUI_NO_EXCEPTIONS}
    ${REZEROGUI_NO_RTTI}
    ${REZEROGUI_WARNING_OPTIONS}
)

# FontAtlas::loadDefaultFont() rasterizes the bundled face at startup. The build
# system knows where the source tree keeps it, so the path is injected instead of
# being hard coded in the header; a shipped binary still falls back to looking for
# the file next to the executable.
target_compile_definitions(ReZeroGui INTERFACE
    REZEROGUI_DEFAULT_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/ProggyForever-Regular.ttf")

if(REZEROGUI_BUILD_EXAMPLES)
    # The Direct3D 11 example needs the Windows SDK.
    if(WIN32)
        add_subdirectory(examples/Dx11Example)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
