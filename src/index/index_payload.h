// index_payload.h — payload layouts of the pulse.exe <-> Pulse.Index.exe pipe.
//
// Header-only so both processes and the payload tests share one definition of
// the bytes. Every message is one Write/Read pair: the codec owns the layout
// and the wire validation, the caller keeps its own policy (what to tell an
// old peer, where results are stored, locking and notifications).
//
// Payloads are a packed little-endian sequence (see ../ipc/protocol.h):
// strings are u32 wchar_count followed by that many UTF-16 code units.
//
// Optional trailing sections exist because older peers stop early. A reader
// must accept the shorter payload and report the missing section through the
// struct's flag instead of failing the message.
#pragma once
#include "../ipc/protocol.h"
#include "index_config.h"
#include "index_types.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pulse::index::wire {

inline uint32_t ClampCount(size_t value) {
    return value > 0xffffffffull ? 0xffffffffu : static_cast<uint32_t>(value);
}

// ---------------------------------------------------------------------------
// RSP_IDX_STATUS: ready, count, text (required) then the optional tail.
// ---------------------------------------------------------------------------
struct StatusWire {
    uint32_t ready = 0;
    uint32_t count = 0;
    std::wstring text;
    // Present since the pinyin index became optional on the host. A host that
    // predates it leaves this section out and is still usable, just without
    // pinyin search, so an absent value reads as 0.
    uint32_t pinyin_ready = 0;
    // Live search needs a revision to detect a stale page. revision and
    // version travel as a pair: a lone revision is a damaged payload, never a
    // usable one. version 1 is the current format; the client decides what to
    // say to a host that reports anything else.
    bool revision_present = false;
    uint64_t revision = 0;
    uint32_t revision_version = 0;
    // Independent of the pair so a host can gain capabilities without them.
    bool capability_present = false;
    uint32_t capability = 0;
};

inline std::vector<uint8_t> WriteStatus(const StatusWire& status) {
    ipc::PayloadWriter w;
    w.PutU32(status.ready);
    w.PutU32(status.count);
    w.PutString(status.text);
    w.PutU32(status.pinyin_ready);
    if (status.revision_present) {
        w.PutU64(status.revision);
        w.PutU32(status.revision_version);
    }
    if (status.capability_present) w.PutU32(status.capability);
    return w.data();
}

// PayloadReader does not advance past a field it could not read, so a short
// tail simply leaves the later sections absent.
inline bool ReadStatus(const uint8_t* p, size_t n, StatusWire& out) {
    ipc::PayloadReader r(p, n);
    StatusWire status;
    if (!r.GetU32(status.ready) || !r.GetU32(status.count) || !r.GetString(status.text)) return false;
    if (!r.GetU32(status.pinyin_ready)) status.pinyin_ready = 0;
    uint64_t revision = 0;
    uint32_t version = 0;
    if (r.GetU64(revision) && r.GetU32(version)) {
        status.revision_present = true;
        status.revision = revision;
        status.revision_version = version;
    }
    uint32_t capability = 0;
    if (r.GetU32(capability)) {
        status.capability_present = true;
        status.capability = capability;
    }
    out = std::move(status);
    return true;
}

// ---------------------------------------------------------------------------
// REQ_IDX_SEARCH: flags, sort, limit, offset, needle, path_prefix, [session_id].
// ---------------------------------------------------------------------------
// Flags bits; the rest of the request fields follow in wire order.
inline constexpr uint32_t kQueryRank = 1;
inline constexpr uint32_t kQueryFoldersOnly = 2;
inline constexpr uint32_t kQuerySortDesc = 4;
inline constexpr uint32_t kQuerySubscribe = 8;
// Bounds the host enforces on what a client may ask for, so a malformed or
// hostile request fails instead of allocating and scanning for it.
inline constexpr size_t kMaxQueryNeedleChars = 4096;
inline constexpr size_t kMaxQueryPathPrefixChars = 32768;

inline std::vector<uint8_t> WriteQuery(const Query& q) {
    ipc::PayloadWriter w;
    uint32_t flags = 0;
    if (q.rank) flags |= kQueryRank;
    if (q.folders_only) flags |= kQueryFoldersOnly;
    if (q.sort_desc) flags |= kQuerySortDesc;
    if (q.subscribe) flags |= kQuerySubscribe;
    w.PutU32(flags);
    w.PutU32(static_cast<uint32_t>(q.sort));
    w.PutU32(static_cast<uint32_t>(q.limit));
    w.PutU32(static_cast<uint32_t>(q.offset));
    w.PutString(q.needle);
    w.PutString(q.path_prefix);
    w.PutU64(q.session_id);
    return w.data();
}

// The session id is the newest field: a client that predates live search
// leaves it out and keeps working (without a subscription), but a trailing
// byte that is not a whole session id means a damaged request.
inline bool ReadQuery(const uint8_t* p, size_t n, Query& out) {
    ipc::PayloadReader r(p, n);
    uint32_t flags = 0, sort = 0, limit = 0, offset = 0;
    std::wstring needle, path_prefix;
    if (!r.GetU32(flags) || !r.GetU32(sort) || !r.GetU32(limit) || !r.GetU32(offset) ||
        !r.GetString(needle) || !r.GetString(path_prefix))
        return false;
    if (sort > static_cast<uint32_t>(ResultSort::Mtime) || needle.size() > kMaxQueryNeedleChars ||
        path_prefix.size() > kMaxQueryPathPrefixChars)
        return false;
    Query query;
    query.rank = (flags & kQueryRank) != 0;
    query.folders_only = (flags & kQueryFoldersOnly) != 0;
    query.sort_desc = (flags & kQuerySortDesc) != 0;
    query.subscribe = (flags & kQuerySubscribe) != 0;
    if (r.remaining() && !r.GetU64(query.session_id)) return false;
    query.needle = std::move(needle);
    query.path_prefix = std::move(path_prefix);
    query.sort = static_cast<ResultSort>(sort);
    query.limit = limit;
    query.offset = offset;
    if (query.limit > kSearchPageCap) query.limit = kSearchPageCap;
    out = std::move(query);
    return true;
}

// ---------------------------------------------------------------------------
// RSP_IDX_SEARCH: total, hit_count, hits, [revision].
// ---------------------------------------------------------------------------
inline std::vector<uint8_t> WriteSearch(const SearchResult& result) {
    ipc::PayloadWriter w;
    w.PutU32(ClampCount(result.total));
    w.PutU32(static_cast<uint32_t>(result.hits.size()));
    for (const auto& hit : result.hits) {
        w.PutString(hit.path);
        w.PutString(hit.name);
        w.PutU32(hit.is_dir ? 1u : 0u);
        w.PutU32(static_cast<uint32_t>(hit.size));
        w.PutU32(static_cast<uint32_t>(hit.size >> 32));
        w.PutU32(static_cast<uint32_t>(hit.mtime));
        w.PutU32(static_cast<uint32_t>(hit.mtime >> 32));
    }
    w.PutU64(result.revision);
    return w.data();
}

inline bool ReadSearch(const uint8_t* p, size_t n, SearchResult& out) {
    ipc::PayloadReader r(p, n);
    uint32_t total = 0, hit_count = 0;
    if (!r.GetU32(total) || !r.GetU32(hit_count)) return false;
    SearchResult result;
    result.total = total;
    result.hits.reserve(hit_count);
    for (uint32_t i = 0; i < hit_count; ++i) {
        Hit hit;
        uint32_t flags = 0, size_lo = 0, size_hi = 0, mtime_lo = 0, mtime_hi = 0;
        if (!r.GetString(hit.path) || !r.GetString(hit.name) || !r.GetU32(flags) ||
            !r.GetU32(size_lo) || !r.GetU32(size_hi) || !r.GetU32(mtime_lo) || !r.GetU32(mtime_hi))
            return false;
        hit.is_dir = (flags & 1u) != 0;
        hit.size = (static_cast<uint64_t>(size_hi) << 32) | size_lo;
        hit.mtime = (static_cast<uint64_t>(mtime_hi) << 32) | mtime_lo;
        result.hits.push_back(std::move(hit));
    }
    // Optional trailing revision: a host that predates it leaves it out, and a
    // damaged one must not throw away the page decoded so far.
    r.GetU64(result.revision);
    out = std::move(result);
    return true;
}

// ---------------------------------------------------------------------------
// RSP_IDX_VOLUMES: as_service, data_dir, volumes, then the scope tail.
// ---------------------------------------------------------------------------
struct VolumesWire {
    bool as_service = false;
    std::wstring data_dir;
    std::vector<VolumeInfo> volumes;
    // Exclusions the user configured. A truncated list keeps the paths read
    // before the cut: an old host that only sends a prefix is still usable.
    std::vector<std::wstring> excluded_paths;
    // The system folder switch is the newest section (service mode only), so
    // its absence is normal. When the section is missing or cut mid-list both
    // lists below are cleared: a half-read list would show wrong folders as
    // excluded, so system_known=false means "no data", never "partial data".
    bool system_known = false;
    bool exclude_system = false;
    std::vector<std::wstring> system_groups;
    std::vector<std::wstring> system_paths;
};

inline std::vector<uint8_t> WriteVolumes(const VolumesWire& volumes) {
    ipc::PayloadWriter w;
    w.PutU32(volumes.as_service ? 1u : 0u);
    w.PutString(volumes.data_dir);
    w.PutU32(static_cast<uint32_t>(volumes.volumes.size()));
    for (const auto& volume : volumes.volumes) {
        w.PutString(volume.id);
        w.PutString(volume.label);
        w.PutString(volume.mount_point);
        w.PutString(volume.file_system);
        w.PutString(volume.state);
        w.PutString(volume.error);
        uint32_t flags = volume.online ? 1u : 0u;
        if (volume.supported) flags |= 2u;
        if (volume.enabled) flags |= 4u;
        w.PutU32(flags);
        w.PutU32(static_cast<uint32_t>(volume.kind));
        w.PutU32(volume.progress);
        w.PutU32(static_cast<uint32_t>(volume.indexed_items));
        w.PutU32(static_cast<uint32_t>(volume.indexed_items >> 32));
    }
    if (volumes.as_service) {
        // Sent after the volume rows so newer clients receive the exclusion list.
        w.PutU32(static_cast<uint32_t>(volumes.excluded_paths.size()));
        for (const auto& path : volumes.excluded_paths) w.PutString(path);
        // Appended for clients that show the system folder switch; older
        // clients stop reading after the user list.
        w.PutU32(volumes.exclude_system ? 1u : 0u);
        w.PutU32(static_cast<uint32_t>(volumes.system_groups.size()));
        for (const auto& group : volumes.system_groups) w.PutString(group);
        w.PutU32(static_cast<uint32_t>(volumes.system_paths.size()));
        for (const auto& path : volumes.system_paths) w.PutString(path);
    } else {
        // Session mode has no machine config: one count of zero stands for
        // "no exclusions and no system section".
        w.PutU32(0);
    }
    return w.data();
}

inline bool ReadVolumes(const uint8_t* p, size_t n, VolumesWire& out) {
    ipc::PayloadReader r(p, n);
    uint32_t service = 0, count = 0;
    VolumesWire volumes;
    if (!r.GetU32(service) || !r.GetString(volumes.data_dir) || !r.GetU32(count)) return false;
    volumes.as_service = service != 0;
    volumes.volumes.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        VolumeInfo volume;
        uint32_t flags = 0, kind = 0, progress = 0, indexed_lo = 0, indexed_hi = 0;
        if (!r.GetString(volume.id) || !r.GetString(volume.label) || !r.GetString(volume.mount_point) ||
            !r.GetString(volume.file_system) || !r.GetString(volume.state) || !r.GetString(volume.error) ||
            !r.GetU32(flags) || !r.GetU32(kind) || !r.GetU32(progress) || !r.GetU32(indexed_lo) ||
            !r.GetU32(indexed_hi))
            return false;
        volume.online = (flags & 1u) != 0;
        volume.supported = (flags & 2u) != 0;
        volume.enabled = (flags & 4u) != 0;
        volume.kind = static_cast<VolumeKind>(kind);
        volume.progress = progress;
        volume.indexed_items = (static_cast<uint64_t>(indexed_hi) << 32) | indexed_lo;
        volumes.volumes.push_back(std::move(volume));
    }
    // Absent when the host is older than the exclusion list (or runs in
    // session mode); a cut list keeps what arrived.
    uint32_t excluded_count = 0;
    if (r.GetU32(excluded_count)) {
        volumes.excluded_paths.reserve(excluded_count);
        for (uint32_t i = 0; i < excluded_count; ++i) {
            std::wstring path;
            if (!r.GetString(path)) break;
            volumes.excluded_paths.push_back(std::move(path));
        }
    }
    uint32_t system_flags = 0, group_count = 0, system_count = 0;
    bool system_known = r.GetU32(system_flags) && r.GetU32(group_count);
    for (uint32_t i = 0; system_known && i < group_count; ++i) {
        std::wstring group;
        system_known = r.GetString(group);
        if (system_known) volumes.system_groups.push_back(std::move(group));
    }
    system_known = system_known && r.GetU32(system_count);
    for (uint32_t i = 0; system_known && i < system_count; ++i) {
        std::wstring path;
        system_known = r.GetString(path);
        if (system_known) volumes.system_paths.push_back(std::move(path));
    }
    volumes.system_known = system_known;
    volumes.exclude_system = system_known && (system_flags & 1u) != 0;
    if (!system_known) {
        volumes.system_groups.clear();
        volumes.system_paths.clear();
    }
    out = std::move(volumes);
    return true;
}

} // namespace pulse::index::wire
