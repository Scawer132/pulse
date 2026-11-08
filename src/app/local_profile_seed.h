// local_profile_seed.h - start a local-profile folder from the installed copy.
//
// A build configured with PULSE_LOCAL_PROFILE keeps its state in its own folder;
// a fresh folder has none of the user's settings, so it imports the installed
// release's configuration once. Only whole files that carry the user's choices
// travel; caches, logs, thumbnails and index data stay behind.
#pragma once

namespace pulse::app {

// False in builds that do not keep a separate profile, when the release folder
// has nothing to import, or (without `force`) when the profile already exists.
// `force` replaces existing files, backing them up first; that is what
// --sync-release-config uses.
bool SeedLocalProfileFromRelease(bool force);

} // namespace pulse::app
