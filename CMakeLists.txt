cmake_minimum_required(VERSION 3.15)
project(GARAM VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# ── Download CLAP headers if not present ──────────────────────────
include(FetchContent)
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(clap)

# ── Build shared library ──────────────────────────────────────────
add_library(GARAM MODULE garam.cpp)

target_include_directories(GARAM PRIVATE
    ${clap_SOURCE_DIR}/include
)

# Remove "lib" prefix
set_target_properties(GARAM PROPERTIES PREFIX "")

# Platform-specific output extension
if(WIN32)
    set_target_properties(GARAM PROPERTIES SUFFIX ".clap")
elseif(APPLE)
    set_target_properties(GARAM PROPERTIES SUFFIX ".clap")
    set_target_properties(GARAM PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.rajvik.garam"
    )
else()
    set_target_properties(GARAM PROPERTIES SUFFIX ".clap")
endif()

# ── Install target ────────────────────────────────────────────────
if(WIN32)
    install(TARGETS GARAM DESTINATION "C:/Program Files/Common Files/CLAP")
elseif(APPLE)
    install(TARGETS GARAM DESTINATION "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
else()
    install(TARGETS GARAM DESTINATION "$ENV{HOME}/.clap")
endif()

message(STATUS "GARAM plugin configured. Run: cmake --build . && cmake --install .")
