# Local-only fork additions (rules: local/merge-policy.md).
#
# Everything this fork adds to the build lives here so CMakeLists.txt keeps a
# single include line as its patch surface across upstream merges. Targets are
# created from pulse_local_targets(), deferred to the end of the top-level
# directory so they inherit upstream's directory-wide compile options.

# Keeps a local build out of the installed release's %LOCALAPPDATA%\Pulse
# (app.json, session.json, places) and gives it a second single instance, so
# both can run side by side. Local-only: never part of a shipped build.
option(PULSE_LOCAL_PROFILE "Keep this build's state under %LOCALAPPDATA%\\Pulse-local" OFF)

# Shown in About only. version.txt and every parser (update check, file version,
# diagnostics) keep the plain upstream release number.
set(PULSE_VERSION_DISPLAY "${PROJECT_VERSION}")
if(PULSE_LOCAL_PROFILE)
    string(APPEND PULSE_VERSION_DISPLAY "-local")
endif()

# Marks one executable as part of the local profile build; absent on release.
function(pulse_local_profile target)
    if(NOT PULSE_LOCAL_PROFILE OR NOT TARGET ${target})
        return()
    endif()
    target_compile_definitions(${target} PRIVATE PULSE_LOCAL_PROFILE=1)
endfunction()

function(pulse_local_targets)
    add_executable(pulse_index_payload_test src/bench/index_payload_test.cpp)
    add_executable(pulse_wndproc_guard_test src/bench/wndproc_guard_test.cpp src/app/wndproc_guard.cpp)
    target_link_libraries(pulse_wndproc_guard_test PRIVATE pulse_runtime_log)
    pulse_local_profile(pulse)
    # The helper this build spawns keeps its index (and network index) under the
    # local profile folder instead of the installed copy's per-user index.
    pulse_local_profile(pulse_index)
endfunction()

cmake_language(DEFER CALL pulse_local_targets)
