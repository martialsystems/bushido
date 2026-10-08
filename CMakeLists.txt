cmake_minimum_required(VERSION 3.22)
project(BUSHIDO VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to a JUCE 8 checkout")
add_subdirectory(${JUCE_DIR} JUCE)

# Framework-free core (no JUCE, no other synth types): module contract, patch graph, cable model, BUSHIDO engine
add_library(rack_core STATIC rack/PatchGraph.cpp rack/CableModel.cpp engine/BushidoModule.cpp)
target_include_directories(rack_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Shared Jidai Cable Standard headers (pitch, detector, volts), vendored unmodified; see third_party/jidai-common/COMMIT.
target_include_directories(rack_core SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jidai-common/include)
set_target_properties(rack_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Bit-exact floats on every compiler and CPU: no FMA contraction (aarch64, or x86 with FMA, would otherwise fuse a * b + c). The engine,
# the tests' exact compares and the browser port (web/test_bushido_parity.js) then agree everywhere. PUBLIC: the plugin and the tests
# that link rack_core get it too. MSVC: /fp:precise, its default, stated (VS 2022 keeps contraction off unless /fp:contract is given).
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(rack_core PUBLIC /fp:precise)
elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")   # clang-cl
    target_compile_options(rack_core PUBLIC /clang:-ffp-contract=off)
else()
    target_compile_options(rack_core PUBLIC -ffp-contract=off)
endif()

juce_add_binary_data(BushidoAssets SOURCES assets/bushido_panel_bg.svg assets/bushido_layout.json assets/bushido_patterns.json)

juce_add_plugin(BUSHIDO
    COMPANY_NAME "Martial Systems"
    BUNDLE_ID com.martialsystems.bushido
    PLUGIN_MANUFACTURER_CODE Mrsy
    PLUGIN_CODE Bshd
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "BUSHIDO"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(BUSHIDO PRIVATE plugin/PluginProcessor.cpp plugin/PluginEditor.cpp ui/Layout.cpp ui/RackPanel.cpp ui/CableLayer.cpp ui/PatternScreen.cpp ui/BushidoTabs.cpp)
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
add_executable(test_redesign tests/test_redesign.cpp)
target_link_libraries(test_redesign PRIVATE rack_core)
add_test(NAME redesign COMMAND test_redesign)

# Headless plugin test (JUCE, no editor): BushidoProcessor state, MIDI out, sub-blocks, bypass, HOST sync.
# It links the plugin's shared code (the BUSHIDO target) and takes its JUCE include paths and definitions.
add_executable(test_plugin tests/test_plugin.cpp)
target_link_libraries(test_plugin PRIVATE BUSHIDO rack_core)
target_include_directories(test_plugin PRIVATE $<TARGET_PROPERTY:BUSHIDO,INCLUDE_DIRECTORIES>)
target_compile_definitions(test_plugin PRIVATE $<TARGET_PROPERTY:BUSHIDO,COMPILE_DEFINITIONS>)
add_test(NAME plugin COMMAND test_plugin)
