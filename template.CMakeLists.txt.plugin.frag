# begin AAP specifics -->  
  
# They are needed in desktop too, for generate-aap-metadata.  
include_directories(  
  "${AAP_DIR}/include"  
)  
  
add_definitions([[-DJUCE_PUSH_NOTIFICATIONS_ACTIVITY="com/rmsl/juce/JuceActivity"]])  
target_compile_definitions(${APP_NAME} PUBLIC  
  JUCEAAP_HAVE_AUDIO_PLAYHEAD_NEW_POSITION_INFO=1  
 JUCE_PUSH_NOTIFICATIONS=1  )  
  
message("AAP_DIR: ${AAP_DIR}")  
message("AAP_JUCE_DIR: ${AAP_JUCE_DIR}")  
juce_add_modules(${AAP_JUCE_DIR}/aap-modules/aap_audio_processors)  
  
if (ANDROID)  
  
  # dependencies  
  find_library(log "log")  
  find_library(android "android")  
  find_library(glesv2 "GLESv2")  
  find_library(egl "EGL")  
  set(cpufeatures_lib "cpufeatures")  
  set(oboe_lib "oboe")  
  
  target_include_directories(${APP_NAME} PRIVATE  
  "${ANDROID_NDK}/sources/android/cpufeatures"  
            "${OBOE_DIR}/include"  
            )  
  
  add_compile_definitions([[JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE=1]])  
endif (ANDROID)  
  
target_link_libraries(${APP_NAME}  
  PRIVATE  
  aap_audio_processors
  )  
# <-- end Android specifics  
# <-- end AAP specifics

