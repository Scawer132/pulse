// local_profile.h - the %LOCALAPPDATA% folder a build keeps its state in.
//
// A build configured with PULSE_LOCAL_PROFILE (CMake option of the same name)
// gets its own folder and its own single-instance lock, so an everyday local
// build and the installed release never rewrite each other's app.json,
// session.json or places. Everything a build shares with its child processes
// stays where it was: packs, the AI folder, caches and logs.
#pragma once

namespace pulse::local_profile {

#ifdef PULSE_LOCAL_PROFILE
inline constexpr wchar_t kFolderName[] = L"Pulse-local";
inline constexpr wchar_t kInstanceName[] = L"Local\\Pulse-local.Singleton";
#else
inline constexpr wchar_t kFolderName[] = L"Pulse";
inline constexpr wchar_t kInstanceName[] = L"Local\\Pulse.Singleton";
#endif

} // namespace pulse::local_profile
