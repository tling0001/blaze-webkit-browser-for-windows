cmake_minimum_required(VERSION 3.25)
project(BlazeCompositionProbe LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Blaze composition probe requires x64 Windows.")
endif()

add_executable(BlazeCompositionProbe CompositionProbe.cpp)
target_compile_features(BlazeCompositionProbe PRIVATE cxx_std_20)
target_compile_definitions(BlazeCompositionProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(BlazeCompositionProbe PRIVATE d3d11 dxgi dcomp ole32)
