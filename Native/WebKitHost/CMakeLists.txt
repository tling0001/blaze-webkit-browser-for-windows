cmake_minimum_required(VERSION 3.25)
project(WebKitHost LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "WebKitHost currently supports only 64-bit Windows (WinCairo).")
endif()

set(WEBKIT_ROOT "" CACHE PATH "Root of a built WebKit Windows port")
find_path(WEBKIT_INCLUDE_DIR WebKit/WKView.h
  HINTS "${WEBKIT_ROOT}/include" "${WEBKIT_ROOT}/WebKitBuild/Release/include"
  REQUIRED)
find_library(WEBKIT_LIBRARY NAMES WebKit2
  HINTS "${WEBKIT_ROOT}/lib64" "${WEBKIT_ROOT}/bin64"
        "${WEBKIT_ROOT}/WebKitBuild/Release/lib64"
        "${WEBKIT_ROOT}/WebKitBuild/Release/bin64"
  REQUIRED)

add_library(WebKitHost SHARED WebKitHost.cpp WebKitHost.def)
target_compile_features(WebKitHost PRIVATE cxx_std_20)
target_compile_definitions(WebKitHost PRIVATE WEBKITHOST_EXPORTS WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(WebKitHost PRIVATE "${WEBKIT_INCLUDE_DIR}")
target_link_libraries(WebKitHost PRIVATE "${WEBKIT_LIBRARY}" user32)
set_target_properties(WebKitHost PROPERTIES
  OUTPUT_NAME "WebKitHost"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
