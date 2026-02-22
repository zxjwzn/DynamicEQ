# ==============================================================================
#  DynamicEQ — CMake build (CI / cross‑platform)
#
#  本文件仅用于 GitHub Actions CI 构建。本地开发继续使用 Projucer + VS2026。
#  JUCE 通过 FetchContent 自动下载，无需本地手动安装。
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(DynamicEQ
    VERSION     1.0.0
    LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# ── Fetch JUCE ───────────────────────────────────────────────────────────────
include(FetchContent)

# 修改此变量以匹配你使用的 JUCE 版本
set(JUCE_GIT_TAG "8.0.4" CACHE STRING "JUCE version (git tag, branch or commit hash)")

FetchContent_Declare(
    juce
    GIT_REPOSITORY  https://github.com/juce-framework/JUCE.git
    GIT_TAG         ${JUCE_GIT_TAG}
    GIT_SHALLOW     ON
)
FetchContent_MakeAvailable(juce)

# ── Plugin Target ────────────────────────────────────────────────────────────
juce_add_plugin(DynamicEQ
    COMPANY_NAME                "yourcompany"
    PLUGIN_MANUFACTURER_CODE    0x4d616e75          # "Manu"
    PLUGIN_CODE                 0x52307078          # "R0px"
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "DynamicEQ"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             "Fx"
)

# 生成 JuceHeader.h（与 Projucer 保持兼容）
juce_generate_juce_header(DynamicEQ)

# ── Sources ──────────────────────────────────────────────────────────────────
target_sources(DynamicEQ
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.h
        Source/DSP/DynamicEQBand.h
        Source/DSP/SpectrumAnalyzer.h
        Source/UI/SpectrumComponent.h
)

target_include_directories(DynamicEQ
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# ── Compile Definitions ─────────────────────────────────────────────────────
target_compile_definitions(DynamicEQ
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

# MSVC: 启用 UTF-8 源码编码（与本地构建的 Directory.Build.props 一致）
if(MSVC)
    target_compile_options(DynamicEQ PRIVATE /utf-8)
endif()

# ── Link JUCE Modules ───────────────────────────────────────────────────────
target_link_libraries(DynamicEQ
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
