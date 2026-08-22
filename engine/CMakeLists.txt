cmake_minimum_required(VERSION 3.20)
project(nekoclaw VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(USE_AVX2 "Enable AVX2 (AVX2+FMA+BMI2)" ON)
option(USE_AVXVNNI "Enable AVX-VNNI (VPDPBUSD) if compiler supports it" OFF)
option(NATIVE_TUNING "Use -march=native (for local builds only, not portable)" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Version
configure_file(${CMAKE_SOURCE_DIR}/../version.h.in ${CMAKE_BINARY_DIR}/version.h @ONLY)

# Core flags — highly optimized
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto -DNDEBUG -fno-exceptions -fno-rtti")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -flto -DNDEBUG -fno-exceptions -fno-rtti")
set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g -fno-omit-frame-pointer")

if(NATIVE_TUNING)
  add_compile_options(-march=native)
else()
  # x86-64-v3 = AVX2 baseline (Haswell+). Your i5-1335U is x86-64-v4 but v3 is portable for Kaggle / older cloud
  if(USE_AVX2)
    add_compile_options(-march=x86-64-v3)
    add_compile_definitions(HAS_AVX2)
    message(STATUS "NekoClaw: AVX2 enabled (-march=x86-64-v3)")
  endif()
endif()

if(USE_AVXVNNI)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-mavxvnni" COMPILER_HAS_AVXVNNI)
  if(COMPILER_HAS_AVXVNNI)
    add_compile_options(-mavxvnni)
    add_compile_definitions(HAS_AVXVNNI)
    message(STATUS "NekoClaw: AVX-VNNI enabled")
  else()
    message(WARNING "AVX-VNNI requested but compiler lacks -mavxvnni")
  endif()
endif()

# Also enable BMI2, FMA explicitly if not native
if(USE_AVX2 AND NOT NATIVE_TUNING)
  add_compile_options(-mavx2 -mbmi -mbmi2 -mfma -mpopcnt -mlzcnt)
endif()

# Engine lib
set(NEKOC_CDIR ${CMAKE_SOURCE_DIR})
set(NEKOC_IDIR ${NEKOC_CDIR}/include)
set(NEKOC_SDIR ${NEKOC_CDIR}/src)

set(SOURCES
  ${NEKOC_SDIR}/bitboard.cpp
  ${NEKOC_SDIR}/position.cpp
  ${NEKOC_SDIR}/movegen.cpp
  ${NEKOC_SDIR}/magic.cpp
  ${NEKOC_SDIR}/nnue/features.cpp
  ${NEKOC_SDIR}/nnue/accumulator.cpp
  ${NEKOC_SDIR}/nnue/network.cpp
  ${NEKOC_SDIR}/nnue/simd.cpp
  ${NEKOC_SDIR}/nnue/weights.cpp
  ${NEKOC_SDIR}/search/tt.cpp
  ${NEKOC_SDIR}/search/history.cpp
  ${NEKOC_SDIR}/search/movepick.cpp
  ${NEKOC_SDIR}/search/search.cpp
  ${NEKOC_SDIR}/search/timeman.cpp
  ${NEKOC_SDIR}/uci.cpp
  ${NEKOC_SDIR}/bench.cpp
)

add_library(nekoclaw_lib STATIC ${SOURCES})
target_include_directories(nekoclaw_lib PUBLIC ${NEKOC_IDIR} ${CMAKE_BINARY_DIR})
target_link_libraries(nekoclaw_lib PUBLIC pthread)

# Executable
add_executable(nekoclaw ${NEKOC_SDIR}/main.cpp)
target_link_libraries(nekoclaw PRIVATE nekoclaw_lib)

# Link-time optimization support
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_err)
if(ipo_ok)
  set_property(TARGET nekoclaw_lib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET nekoclaw PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Tests
enable_testing()
add_executable(perft ${CMAKE_SOURCE_DIR}/../tests/perft.cpp)
target_link_libraries(perft PRIVATE nekoclaw_lib)
add_test(NAME perft COMMAND perft)

# Install
install(TARGETS nekoclaw DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/../LICENSE DESTINATION share/doc/nekoclaw)
