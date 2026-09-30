cmake_minimum_required(VERSION 3.22)
project(ElmioEfectos VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

# JUCE: usa una copia local si se pasa -DJUCE_PATH=..., si no la descarga.
set(JUCE_PATH "" CACHE PATH "Ruta a una copia local de JUCE (opcional)")
if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

add_subdirectory(plugins/TocadiscosStop)

# Prueba offline del motor (no necesita JUCE).
option(ELMIO_BUILD_TESTS "Compilar las pruebas offline" ON)
if(ELMIO_BUILD_TESTS)
    add_executable(render_test tests/render_test.cpp)
    target_include_directories(render_test PRIVATE plugins/TocadiscosStop/Source)
    enable_testing()
    add_test(NAME tocadiscos_render COMMAND render_test ${CMAKE_BINARY_DIR}/tocadiscos_stop_demo.wav)
endif()
