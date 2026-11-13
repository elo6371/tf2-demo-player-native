# ---------------------------------------------------------------------------
# T75 (2026-10-28): split out of native/CMakeLists.txt (HEAD db3316e).
#
# global settings and the shared demo-header contract.
#
# The text below is the ORIGINAL lines 4-30 of native/CMakeLists.txt, moved
# verbatim: no target name, source list, link line, compile definition, compile
# option, install rule or cache variable was edited, added or re-ordered.  The
# include() order in ../CMakeLists.txt is the original statement order, so
# target creation order -- which is the NMAKE build order, and therefore which
# targets a failed link truncates -- is unchanged as well.
#
# Targets defined here: -
# T75-BODY-START
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WIN32_EXECUTABLE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# T90 (2026-11-07): every binary this tree links must be a function of its
# inputs, not of when nmake happened to run. Without this, two builds of one
# source tree differ in the COFF TimeDateStamp and the embedded debug records,
# which is how "exe sha256" came to be mistaken for an identity: three builds of
# the same tree measured three different hashes, and the PE header timestamp
# moved by 14 s between two of them. /Brepro is the documented MSVC switch on
# BOTH sides (cl and link); the NMake generator adds it to neither on its own.
# Measured by build-identity-check.sh --reproducible, which builds the whole tree
# cleanly twice and compares bytes -- the flag is not taken on faith.
if (MSVC)
  add_compile_options(/Brepro)
  add_link_options(/Brepro)
endif()

# T9 (2026-10-16): build-graph repair, no behaviour change.
#
# The audio layer moved buildSoundTimeline into demo_header.cpp, where it calls
# into audio_timeline.cpp, which in turn needs audio_resource.cpp (WAV/MP3
# classification) and the winmm/msacm32 libraries that file links against. Any
# target that compiles demo_header.cpp therefore needs all of that, and fourteen
# of them did not have it, so `cmake --build` stopped at the first one
# (LNK1120 on demo_open_probe, four symbols; then three more one layer down,
# then two more in presentation_probe, then four in viewmodel_fov_probe).
#
# Reproducible at the T9 base commit with demo_header.cpp byte-identical to the
# base and every one of those target blocks untouched, so this is a merge break
# that predates T9 and blocked every build, not a T9 change. Listed once here
# because this same list was already silently short in fourteen places; a
# fifteenth copy of it is how it got that way.
set(TF2_DEMO_HEADER_SOURCES src/demo_header.cpp src/demo_header_core.cpp src/demo_header_net.cpp src/demo_header_entity.cpp src/demo_header_strtab.cpp src/demo_header_sound.cpp src/demo_header_events.cpp src/demo_header_track.cpp src/audio_timeline.cpp src/audio_resource.cpp src/vpk_archive.cpp third_party/lzma/LzmaDec.c)
set(TF2_DEMO_HEADER_LIBS winmm msacm32)
# audio_resource.cpp calls std::min on values from <windows.h>, so a target that
# compiles it without NOMINMAX fails at `std::min` (C2589 "illegal token on right
# side of ::", because the min macro eats the call). The product target already
# carried these two; the probe copies did not.
set(TF2_DEMO_HEADER_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)
