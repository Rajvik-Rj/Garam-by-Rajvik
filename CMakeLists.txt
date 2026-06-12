cmake_minimum_required(VERSION 3.15)
project(GARAM VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(JUCE)

juce_add_plugin(GARAM
    PLUGIN_MANUFACTURER_CODE Rajv
    PLUGIN_CODE Gram
    FORMATS VST3
    PRODUCT_NAME "GARAM"
    COMPANY_NAME "Rajvik"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(GARAM)
target_sources(GARAM PRIVATE garam_juce.cpp)
target_compile_definitions(GARAM PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(GARAM PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)
