cmake_minimum_required(VERSION 3.22)

project(Spe VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MIXOLVE_SPE_USE_LOCAL_JUCE "Use a local JUCE checkout via JUCE_DIR" OFF)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)

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

juce_add_binary_data(spe_assets
    SOURCES
        assets/fonts/SometypeMono-Bold.ttf
        assets/fonts/SometypeMono-Regular.ttf
        assets/fonts/SometypeMono-OFL.txt
)

set(SPE_PLUGIN_ARGS
    COMPANY_NAME "mixolve"
    BUNDLE_ID "com.mixolve.spe"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_MANUFACTURER_CODE Mxol
    PLUGIN_CODE Spe1
    FORMATS VST3
    PRODUCT_NAME "spe"
)

if(APPLE)
    list(APPEND SPE_PLUGIN_ARGS
        COPY_PLUGIN_AFTER_BUILD TRUE
        VST3_COPY_DIR "/Library/Audio/Plug-Ins/ALL"
    )
endif()

juce_add_plugin(Spe ${SPE_PLUGIN_ARGS})

juce_generate_juce_header(Spe)

target_sources(Spe
    PRIVATE
        source/PluginEditor.cpp
        source/PluginEditor.h
        source/PluginProcessor.cpp
        source/PluginProcessor.h
)

target_compile_definitions(Spe
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Spe
    PRIVATE
        spe_assets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

set(SPE_PACKAGE_STAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spe.vst3")

if(APPLE)
    set(SPE_PACKAGE_ZIP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/spe-macos.zip")
elseif(WIN32)
    set(SPE_PACKAGE_ZIP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/spe-windows.zip")
else()
    set(SPE_PACKAGE_ZIP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/spe.zip")
endif()

add_custom_command(TARGET Spe_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${SPE_PACKAGE_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:Spe_VST3>" "${SPE_PACKAGE_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${SPE_PACKAGE_ZIP_PATH}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_SOURCE_DIR}" ${CMAKE_COMMAND} -E tar "cf" "${SPE_PACKAGE_ZIP_PATH}" --format=zip -- "spe.vst3"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${SPE_PACKAGE_STAGE_DIR}"
    VERBATIM
)
