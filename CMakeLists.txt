cmake_minimum_required(VERSION 3.22)

# Loading aap-juce does not configure JUCE or modify any source tree.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(aap-juce LANGUAGES NONE)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/AAPJuce.cmake")
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(AAP_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
endif()
