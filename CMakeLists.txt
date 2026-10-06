cmake_minimum_required(VERSION 3.22)
project(BUSHIDO VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to a JUCE 8 checkout")
add_subdirectory(${JUCE_DIR} JUCE)

# Framework-free core (no JUCE, no other synth types): module contract, patch graph, cable model, BUSHIDO engine
add_library(rack_core STATIC rack/PatchGraph.cpp rack/CableModel.cpp engine/Sq10Module.cpp)
target_include_directories(rack_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(rack_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_binary_data(BushidoAssets SOURCES assets/sq10_panel_bg.svg assets/sq10_layout.json assets/sq10_patterns.json)

juce_add_plugin(BUSHIDO
    COMPANY_NAME "Martial Systems"
    PLUGIN_MANUFACTURER_CODE Sqcl
    PLUGIN_CODE Sq10
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "BUSHIDO"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(BUSHIDO PRIVATE plugin/PluginProcessor.cpp plugin/PluginEditor.cpp ui/Layout.cpp ui/RackPanel.cpp ui/CableLayer.cpp ui/PatternScreen.cpp)
target_compile_definitions(BUSHIDO PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(BUSHIDO PRIVATE rack_core BushidoAssets juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Tests (no JUCE needed)
enable_testing()
add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine PRIVATE rack_core)
add_test(NAME engine COMMAND test_engine)
add_executable(test_cables tests/test_cables.cpp)
target_link_libraries(test_cables PRIVATE rack_core)
add_test(NAME cables COMMAND test_cables)
