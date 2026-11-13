# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# audio effects / schedule / timeline census / content map / corpus scan / winmm sink.
#
# The text below is the ORIGINAL lines 595-664 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: audio_effects_probe audio_scheduler_probe sound_timeline_census_probe sound_content_map_probe sound_corpus_scan_probe winmm_sink_probe
# T75-BODY-START
# ---------------------------------------------------------------------------
add_executable(audio_effects_probe tools/audio_effects_probe.cpp src/audio_player.cpp src/audio_timeline.cpp src/audio_resource.cpp src/effects_timeline.cpp src/pcf_operators.cpp src/playback_hud.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
target_include_directories(audio_effects_probe PRIVATE include third_party/lzma)
target_compile_definitions(audio_effects_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(audio_effects_probe PRIVATE winmm msacm32)
set_target_properties(audio_effects_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(audio_scheduler_probe tools/audio_scheduler_probe.cpp src/audio_player.cpp src/audio_timeline.cpp src/audio_resource.cpp src/vpk_archive.cpp)
target_include_directories(audio_scheduler_probe PRIVATE include)
target_compile_definitions(audio_scheduler_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(audio_scheduler_probe PRIVATE winmm msacm32)
set_target_properties(audio_scheduler_probe PROPERTIES WIN32_EXECUTABLE OFF)

# Reads a real demo and reports where its decoded sound events sit relative to
# the playback origin. Links demo_header.cpp (the decoder) and audio_timeline.cpp
# (the origin/tick helpers) so the census and the product share one definition of
# the relative tick instead of each keeping its own copy.
add_executable(sound_timeline_census_probe
  tools/sound_timeline_census_probe.cpp
  src/audio_player.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(sound_timeline_census_probe PRIVATE include third_party/lzma)
target_compile_definitions(sound_timeline_census_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
# winmm for AudioPlayer's declarations; the probe injects an outcome sink, so no
# device is opened even though the backend is linked. msacm32 for the MPEG
# Layer III decode path in audio_resource.cpp.
target_link_libraries(sound_timeline_census_probe PRIVATE winmm msacm32)
target_link_libraries(sound_timeline_census_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(sound_timeline_census_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(sound_timeline_census_probe PROPERTIES WIN32_EXECUTABLE OFF)

# The traceable content map: one line per scheduled event from the demo's own
# soundprecache table through the product's archive chain to a named decode
# status, plus the per-kind / per-status counts a gate asserts against. Links
# the same four sources the census links, and winmm only for AudioPlayer's
# declarations -- the scheduler is driven with an injected sink, so no device is
# opened.
add_executable(sound_content_map_probe
  tools/sound_content_map_probe.cpp
  src/audio_player.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(sound_content_map_probe PRIVATE include third_party/lzma)
target_compile_definitions(sound_content_map_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(sound_content_map_probe PRIVATE winmm msacm32)
target_link_libraries(sound_content_map_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(sound_content_map_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(sound_content_map_probe PROPERTIES WIN32_EXECUTABLE OFF)

# Walks a corpus of real recordings and reports, per demo, which sound event
# kinds the demo actually carries: the decoded event kinds, the resolved-name
# kinds, and the raw svc_voice_*/svc_temp_entities message presence. Exists so
# a claim of the form "music events are covered by a real input" can point at a
# named recording instead of at the classifier's ability to name the kind.
# Links the same sources the content map links plus demo_header.cpp's decode
# entry points, and drives the scheduler with an injected sink so no device is
# opened.
add_executable(sound_corpus_scan_probe
  tools/sound_corpus_scan_probe.cpp
  src/audio_player.cpp
  ${TF2_DEMO_HEADER_SOURCES})
target_include_directories(sound_corpus_scan_probe PRIVATE include third_party/lzma)
target_compile_definitions(sound_corpus_scan_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(sound_corpus_scan_probe PRIVATE winmm msacm32)
target_link_libraries(sound_corpus_scan_probe PRIVATE ${TF2_DEMO_HEADER_LIBS})
target_compile_definitions(sound_corpus_scan_probe PRIVATE ${TF2_DEMO_HEADER_DEFINES})
set_target_properties(sound_corpus_scan_probe PROPERTIES WIN32_EXECUTABLE OFF)

add_executable(winmm_sink_probe tools/winmm_sink_probe.cpp)
target_compile_definitions(winmm_sink_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(winmm_sink_probe PRIVATE winmm)
set_target_properties(winmm_sink_probe PROPERTIES WIN32_EXECUTABLE OFF)
