set(FORMATS VST3)
if(APPLE)
    list(APPEND FORMATS AU)
endif()

juce_add_plugin(TocadiscosStop
    PRODUCT_NAME "Tocadiscos Stop"
    COMPANY_NAME "Elmio"
    BUNDLE_ID "com.elmio.tocadiscosstop"
    PLUGIN_MANUFACTURER_CODE Elmo
    PLUGIN_CODE TdSt
    FORMATS ${FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Modulation
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(TocadiscosStop PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(TocadiscosStop PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TocadiscosStop
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
