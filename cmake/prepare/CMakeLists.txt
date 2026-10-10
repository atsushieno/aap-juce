cmake_minimum_required(VERSION 3.22)
project(AAPJucePreparation LANGUAGES NONE)

# A normal host configuration supports add_subdirectory(), without enabling
# compilers or building JUCE. The application's preparation block returns
# before its project() call and native targets.
if(NOT APP_CMAKE OR NOT EXISTS "${APP_CMAKE}")
    message(FATAL_ERROR "APP_CMAKE must identify the patched application entry point")
endif()
set(AAP_JUCE_PREPARE_ONLY TRUE)
include("${APP_CMAKE}")
