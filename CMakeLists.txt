cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)

project(Spe VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MIXOLVE_SPE_USE_LOCAL_JUCE "Use a local JUCE checkout via JUCE_DIR" OFF)

if(APPLE)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE MIXOLVE_SPE_MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(MIXOLVE_SPE_MACOS_SDK_PATH
       AND NOT EXISTS "${MIXOLVE_SPE_MACOS_SDK_PATH}/System/Library/Frameworks/Foundation.framework/Headers/NSGarbageCollector.h")
        set(MIXOLVE_SPE_APPLE_SDK_SHIMS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/apple-sdk-shims")
        set(MIXOLVE_SPE_APPLE_SDK_SHIM_FLAG "-isystem${MIXOLVE_SPE_APPLE_SDK_SHIMS_DIR}")

        string(APPEND CMAKE_C_FLAGS " ${MIXOLVE_SPE_APPLE_SDK_SHIM_FLAG}")
        string(APPEND CMAKE_CXX_FLAGS " ${MIXOLVE_SPE_APPLE_SDK_SHIM_FLAG}")
        string(APPEND CMAKE_OBJC_FLAGS " ${MIXOLVE_SPE_APPLE_SDK_SHIM_FLAG}")
        string(APPEND CMAKE_OBJCXX_FLAGS " ${MIXOLVE_SPE_APPLE_SDK_SHIM_FLAG}")

        set(ENV{CFLAGS} "$ENV{CFLAGS} ${MIXOLVE_SPE_APPLE_SDK_SHIM_FLAG}")
        set(ENV{CXXFLAGS} "$ENV{CXXFLAGS} ${MIXOLVE_SPE_APPLE_SDK_SHIM_FLAG}")
        set(ENV{OBJCFLAGS} "$ENV{OBJCFLAGS} ${MIXOLVE_SPE_APPLE_SDK_SHIM_FLAG}")
        set(ENV{OBJCXXFLAGS} "$ENV{OBJCXXFLAGS} ${MIXOLVE_SPE_APPLE_SDK_SHIM_FLAG}")

        message(STATUS "Using local Foundation shim for missing NSGarbageCollector.h")
    endif()
endif()

if(MIXOLVE_SPE_USE_LOCAL_JUCE)
    if(NOT DEFINED JUCE_DIR)
        message(FATAL_ERROR "Set -DJUCE_DIR=/absolute/path/to/JUCE when MIXOLVE_SPE_USE_LOCAL_JUCE=ON")
    endif()

    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)

    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
    )

    FetchContent_MakeAvailable(juce)
endif()

juce_add_plugin(Spe
    COMPANY_NAME "mixolve"
    BUNDLE_ID "com.mixolve.spe"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "/Library/Audio/Plug-Ins/ALL"
    PLUGIN_MANUFACTURER_CODE Mxol
    PLUGIN_CODE Spe1
    FORMATS VST3
    PRODUCT_NAME "spe"
)

juce_generate_juce_header(Spe)

target_sources(Spe
    PRIVATE
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
)

target_compile_definitions(Spe
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Spe
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

set(SPE_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(SPE_DIST_VST3_DIR "${SPE_DIST_DIR}/spe.vst3")

add_custom_command(TARGET Spe_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${SPE_DIST_VST3_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPE_DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:Spe_VST3>" "${SPE_DIST_VST3_DIR}"
    VERBATIM
)
