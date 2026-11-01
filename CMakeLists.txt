cmake_minimum_required(VERSION 3.22)
project(Groovebox VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# Peka på mappen där du packat upp Steinbergs ASIO SDK (den som innehåller common/ och host/)
set(ASIO_SDK_DIR "" CACHE PATH "Sökväg till ASIO SDK")
# Om du lägger SDK:n i mappen asio-sdk/ bredvid den här filen hittas den automatiskt
if(NOT ASIO_SDK_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/asio-sdk/common/iasiodrv.h")
    set(ASIO_SDK_DIR "${CMAKE_SOURCE_DIR}/asio-sdk")
endif()

juce_add_gui_app(Groovebox PRODUCT_NAME "Groovebox")
target_sources(Groovebox PRIVATE Source/Main.cpp Source/MainComponent.h)

target_compile_definitions(Groovebox PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_ARA=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Groovebox,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${CMAKE_PROJECT_VERSION}")

if(WIN32 AND ASIO_SDK_DIR)
    target_compile_definitions(Groovebox PRIVATE JUCE_ASIO=1)
    target_include_directories(Groovebox PRIVATE
        ${ASIO_SDK_DIR}/common ${ASIO_SDK_DIR}/host ${ASIO_SDK_DIR}/host/pc)
endif()

target_link_libraries(Groovebox PRIVATE
    juce::juce_gui_extra juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
