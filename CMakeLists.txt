cmake_minimum_required(VERSION 3.18)
project(edge_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Release build optimisations ──────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/O2 /Ob2 /Oi /GL /arch:AVX2 /fp:fast /W3)
    add_link_options(/LTCG)
else()
    add_compile_options(-O3 -march=native -ffast-math -funroll-loops -Wall)
endif()

# ── pybind11 ─────────────────────────────────────────────────
find_package(Python COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG)

if(NOT pybind11_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.13.1
    )
    FetchContent_MakeAvailable(pybind11)
endif()

# ── Shared include path ────────────────────────────────────────
set(ENGINE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/core/include)

# ── General-Purpose MT5 Strategy Tester API ──────────────────
pybind11_add_module(edge_tester
    core/src/tester_bindings.cpp
)

target_include_directories(edge_tester PRIVATE ${ENGINE_INCLUDES})

# ── Optional: Parallel STL via TBB ───────────────────────────
find_package(TBB QUIET)
if(TBB_FOUND)
    target_link_libraries(edge_tester PRIVATE TBB::tbb)
    target_compile_definitions(edge_tester PRIVATE EDGE_USE_TBB=1)
    message(STATUS "TBB found — parallel STL enabled")
endif()

# ── Threading ─────────────────────────────────────────────────
find_package(Threads REQUIRED)
target_link_libraries(edge_tester PRIVATE Threads::Threads)

# ── Visibility ────────────────────────────────────────────────
set_target_properties(edge_tester PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ── Install ───────────────────────────────────────────────────
install(TARGETS edge_tester DESTINATION edge_engine)
install(TARGETS edge_tester DESTINATION .)

message(STATUS "EdgeEngine build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Python: ${Python_EXECUTABLE}")
message(STATUS "Module: edge_tester (general-purpose MT5 Strategy Tester replica)")

