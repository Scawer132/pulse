// index_payload_test.cpp — byte layout and old-peer tolerance of the index
// pipe payloads (src/index/index_payload.h). No engine, no pipe: the codec is
// pure, so this compares writer output against the historical byte sequences.
#include "../index/index_payload.h"
#include "../index/index_protocol.h"
#include <iostream>
#include <string>
#include <vector>

namespace ipc = pulse::ipc;
using namespace pulse::index;

namespace {

// A REQ_IDX_SEARCH payload with the fields the writer has always emitted; the
// session id is left out to build the payload of a client that predates it.
std::vector<uint8_t> RawQuery(uint32_t flags, uint32_t sort, uint32_t limit, uint32_t offset,
                              const std::wstring& needle, const std::wstring& path_prefix,
                              bool with_session = false, uint64_t session_id = 0) {
    ipc::PayloadWriter w;
    w.PutU32(flags);
    w.PutU32(sort);
    w.PutU32(limit);
    w.PutU32(offset);
    w.PutString(needle);
    w.PutString(path_prefix);
    if (with_session) w.PutU64(session_id);
    return w.data();
}

} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool value, const char* name) {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << name << '\n';
        failures += !value;
    };

    // --- RSP_IDX_STATUS -----------------------------------------------------
    // Every optional section present: layout, exact bytes and round-trip.
    {
        wire::StatusWire out;
        out.ready = 1;
        out.count = 42;
        out.text = L"索引就绪";
        out.pinyin_ready = 1;
        out.revision_present = true;
        out.revision = 0x1122334455667788ull;
        out.revision_version = 1;
        out.capability_present = true;
        out.capability = kVolumePauseCapability;
        const auto bytes = wire::WriteStatus(out);
        ipc::PayloadReader r(bytes.data(), bytes.size());
        uint32_t ready = 0, count = 0, pinyin = 0, version = 0, capability = 0;
        uint64_t revision = 0;
        std::wstring text;
        check(r.GetU32(ready) && ready == 1, "status layout: u32 ready first");
        check(r.GetU32(count) && count == 42, "status layout: u32 count second");
        check(r.GetString(text) && text == L"索引就绪", "status layout: string text third");
        check(r.GetU32(pinyin) && pinyin == 1, "status layout: u32 pinyin_ready fourth");
        check(r.GetU64(revision) && revision == 0x1122334455667788ull, "status layout: u64 revision fifth");
        check(r.GetU32(version) && version == 1, "status layout: u32 version sixth");
        check(r.GetU32(capability) && capability == kVolumePauseCapability, "status layout: u32 capability last");
        check(r.remaining() == 0, "status layout: no trailing bytes");
        ipc::PayloadWriter expected;
        expected.PutU32(1);
        expected.PutU32(42);
        expected.PutString(L"索引就绪");
        expected.PutU32(1);
        expected.PutU64(0x1122334455667788ull);
        expected.PutU32(1);
        expected.PutU32(kVolumePauseCapability);
        check(bytes == expected.data(), "status: writer bytes unchanged");
        wire::StatusWire back;
        check(wire::ReadStatus(bytes.data(), bytes.size(), back) && back.ready == 1 && back.count == 42 &&
                  back.text == L"索引就绪" && back.pinyin_ready == 1 && back.revision_present &&
                  back.revision == 0x1122334455667788ull && back.revision_version == 1 &&
                  back.capability_present && back.capability == kVolumePauseCapability,
              "status: round-trip with every section");
    }
    {
        // No revision pair, no capability: what a host that predates both sends.
        wire::StatusWire out;
        out.ready = 1;
        out.pinyin_ready = 1;
        const auto bytes = wire::WriteStatus(out);
        wire::StatusWire back;
        check(wire::ReadStatus(bytes.data(), bytes.size(), back) && back.pinyin_ready == 1 &&
                  !back.revision_present && !back.capability_present,
              "status: round-trip without revision and capability");
        ipc::PayloadWriter expected;
        expected.PutU32(1);
        expected.PutU32(0);
        expected.PutString(L"");
        expected.PutU32(1);
        check(bytes == expected.data(), "status: absent sections write nothing");
    }
    {
        // Revision pair without capability (the release host before pause).
        wire::StatusWire out;
        out.ready = 1;
        out.pinyin_ready = 1;
        out.revision_present = true;
        out.revision = 9;
        out.revision_version = 1;
        wire::StatusWire back;
        const auto bytes = wire::WriteStatus(out);
        check(wire::ReadStatus(bytes.data(), bytes.size(), back) && back.revision_present &&
                  back.revision == 9 && back.revision_version == 1 && !back.capability_present,
              "status: round-trip with revision only");
        out.revision_version = 2;
        out.capability_present = true;
        out.capability = kVolumePauseCapability;
        const auto newer = wire::WriteStatus(out);
        check(wire::ReadStatus(newer.data(), newer.size(), back) && back.revision_present &&
                  back.revision_version == 2 && back.capability_present &&
                  back.capability == kVolumePauseCapability,
              "status: unknown revision version still decodes");
    }
    {
        // Three-field payload of the oldest host.
        ipc::PayloadWriter legacy;
        legacy.PutU32(1);
        legacy.PutU32(0);
        legacy.PutString(L"旧宿主");
        wire::StatusWire back;
        check(wire::ReadStatus(legacy.data().data(), legacy.data().size(), back) && back.ready == 1 &&
                  back.count == 0 && back.text == L"旧宿主" && back.pinyin_ready == 0 &&
                  !back.revision_present && !back.capability_present,
              "status: three-field legacy payload accepted");
    }
    {
        // Required field missing: keep rejecting.
        ipc::PayloadWriter truncated;
        truncated.PutU32(1);
        truncated.PutU32(2);
        wire::StatusWire back;
        check(!wire::ReadStatus(truncated.data().data(), truncated.data().size(), back),
              "status: missing text rejected");
    }

    // --- REQ_IDX_SEARCH -----------------------------------------------------
    {
        Query q;
        q.session_id = 0x0123456789abcdefull;
        q.rank = true;
        q.folders_only = true;
        q.sort_desc = true;
        q.subscribe = true;
        q.sort = ResultSort::Mtime;
        q.limit = 123;
        q.offset = 456;
        q.needle = L"report";
        q.path_prefix = L"C:\\Users";
        const auto bytes = wire::WriteQuery(q);
        ipc::PayloadReader r(bytes.data(), bytes.size());
        uint32_t flags = 0, sort = 0, limit = 0, offset = 0;
        uint64_t session_id = 0;
        std::wstring needle, path_prefix;
        check(r.GetU32(flags) && flags == 15u, "query layout: u32 flags first (all bits)");
        check(r.GetU32(sort) && sort == static_cast<uint32_t>(ResultSort::Mtime), "query layout: u32 sort second");
        check(r.GetU32(limit) && limit == 123, "query layout: u32 limit third");
        check(r.GetU32(offset) && offset == 456, "query layout: u32 offset fourth");
        check(r.GetString(needle) && needle == L"report", "query layout: string needle fifth");
        check(r.GetString(path_prefix) && path_prefix == L"C:\\Users", "query layout: string path_prefix sixth");
        check(r.GetU64(session_id) && session_id == 0x0123456789abcdefull, "query layout: u64 session_id last");
        check(r.remaining() == 0, "query layout: no trailing bytes");
        ipc::PayloadWriter expected;
        expected.PutU32(15);
        expected.PutU32(static_cast<uint32_t>(ResultSort::Mtime));
        expected.PutU32(123);
        expected.PutU32(456);
        expected.PutString(L"report");
        expected.PutString(L"C:\\Users");
        expected.PutU64(0x0123456789abcdefull);
        check(bytes == expected.data(), "query: writer bytes unchanged");
        Query back;
        check(wire::ReadQuery(bytes.data(), bytes.size(), back) &&
                  back.session_id == 0x0123456789abcdefull && back.rank && back.folders_only &&
                  back.sort_desc && back.subscribe && back.sort == ResultSort::Mtime && back.limit == 123 &&
                  back.offset == 456 && back.needle == L"report" && back.path_prefix == L"C:\\Users",
              "query: round-trip with session id");
    }
    {
        // Older client: no session id, and the flags it did set must survive.
        const auto bytes = RawQuery(5, 0, 48, 7, L"abc", L"D:\\");
        Query back;
        check(wire::ReadQuery(bytes.data(), bytes.size(), back) && back.session_id == 0 && back.rank &&
                  !back.folders_only && back.sort_desc && !back.subscribe && back.needle == L"abc" &&
                  back.path_prefix == L"D:\\" && back.limit == 48 && back.offset == 7,
              "query: legacy payload without session id accepted");
    }
    {
        // Validation bounds: a host must not scan for a request it cannot serve.
        const uint32_t highest = static_cast<uint32_t>(ResultSort::Mtime);
        const auto valid = RawQuery(0, highest, 48, 0, L"a", L"");
        Query q;
        check(wire::ReadQuery(valid.data(), valid.size(), q) && q.sort == ResultSort::Mtime,
              "query: highest sort accepted");
        const auto bad_sort = RawQuery(0, highest + 1, 48, 0, L"a", L"");
        check(!wire::ReadQuery(bad_sort.data(), bad_sort.size(), q), "query: sort past Mtime rejected");
        const auto longest = RawQuery(0, 0, 48, 0, std::wstring(4096, L'a'), L"");
        check(wire::ReadQuery(longest.data(), longest.size(), q) && q.needle.size() == 4096,
              "query: 4096-char needle accepted");
        const auto too_long = RawQuery(0, 0, 48, 0, std::wstring(4097, L'a'), L"");
        check(!wire::ReadQuery(too_long.data(), too_long.size(), q), "query: 4097-char needle rejected");
        const auto long_prefix = RawQuery(0, 0, 48, 0, L"a", std::wstring(32768, L'p'));
        check(wire::ReadQuery(long_prefix.data(), long_prefix.size(), q) && q.path_prefix.size() == 32768,
              "query: 32768-char path prefix accepted");
        const auto too_long_prefix = RawQuery(0, 0, 48, 0, L"a", std::wstring(32769, L'p'));
        check(!wire::ReadQuery(too_long_prefix.data(), too_long_prefix.size(), q),
              "query: 32769-char path prefix rejected");
        const auto clamped = RawQuery(1, 0, static_cast<uint32_t>(kSearchPageCap) + 1, 0, L"a", L"");
        check(wire::ReadQuery(clamped.data(), clamped.size(), q) && q.limit == kSearchPageCap,
              "query: limit above page cap clamped");
        const auto at_cap = RawQuery(1, 0, static_cast<uint32_t>(kSearchPageCap), 0, L"a", L"");
        check(wire::ReadQuery(at_cap.data(), at_cap.size(), q) && q.limit == kSearchPageCap,
              "query: limit at page cap kept");
        auto partial = RawQuery(1, 0, 48, 0, L"a", L"");
        ipc::PayloadWriter half;
        half.PutU32(7);
        partial.insert(partial.end(), half.data().begin(), half.data().end());
        check(!wire::ReadQuery(partial.data(), partial.size(), q), "query: half a session id rejected");
        const std::vector<uint8_t> empty;
        check(!wire::ReadQuery(empty.data(), empty.size(), q), "query: empty payload rejected");
    }

    // --- RSP_IDX_SEARCH -----------------------------------------------------
    {
        SearchResult sr;
        sr.total = 3;
        sr.revision = 0xdeadbeefcafe1234ull;
        Hit dir;
        dir.path = L"C:\\Windows";
        dir.name = L"Windows";
        dir.is_dir = true;
        Hit file;
        file.path = L"C:\\data\\big.bin";
        file.name = L"big.bin";
        file.size = 0x1234567890abcdefull;
        file.mtime = 0x0102030405060708ull;
        sr.hits = {dir, file};
        const auto bytes = wire::WriteSearch(sr);
        ipc::PayloadReader r(bytes.data(), bytes.size());
        uint32_t total = 0, hit_count = 0;
        check(r.GetU32(total) && total == 3, "search layout: u32 total first");
        check(r.GetU32(hit_count) && hit_count == 2, "search layout: u32 hit_count second");
        std::wstring path, name;
        uint32_t is_dir = 0, size_lo = 0, size_hi = 0, mtime_lo = 0, mtime_hi = 0;
        check(r.GetString(path) && path == L"C:\\Windows" && r.GetString(name) && name == L"Windows" &&
                  r.GetU32(is_dir) && is_dir == 1 && r.GetU32(size_lo) && size_lo == 0 &&
                  r.GetU32(size_hi) && size_hi == 0 && r.GetU32(mtime_lo) && mtime_lo == 0 &&
                  r.GetU32(mtime_hi) && mtime_hi == 0,
              "search layout: hit 0 fields in order");
        check(r.GetString(path) && path == L"C:\\data\\big.bin" && r.GetString(name) && name == L"big.bin" &&
                  r.GetU32(is_dir) && is_dir == 0 && r.GetU32(size_lo) && size_lo == 0x90abcdefu &&
                  r.GetU32(size_hi) && size_hi == 0x12345678u && r.GetU32(mtime_lo) && mtime_lo == 0x05060708u &&
                  r.GetU32(mtime_hi) && mtime_hi == 0x01020304u,
              "search layout: hit 1 splits size and mtime lo/hi");
        uint64_t revision = 0;
        check(r.GetU64(revision) && revision == 0xdeadbeefcafe1234ull, "search layout: u64 revision last");
        check(r.remaining() == 0, "search layout: no trailing bytes");
        ipc::PayloadWriter expected;
        expected.PutU32(3);
        expected.PutU32(2);
        expected.PutString(L"C:\\Windows");
        expected.PutString(L"Windows");
        expected.PutU32(1);
        expected.PutU32(0);
        expected.PutU32(0);
        expected.PutU32(0);
        expected.PutU32(0);
        expected.PutString(L"C:\\data\\big.bin");
        expected.PutString(L"big.bin");
        expected.PutU32(0);
        expected.PutU32(0x90abcdefu);
        expected.PutU32(0x12345678u);
        expected.PutU32(0x05060708u);
        expected.PutU32(0x01020304u);
        expected.PutU64(0xdeadbeefcafe1234ull);
        check(bytes == expected.data(), "search: writer bytes unchanged");
        SearchResult back;
        check(wire::ReadSearch(bytes.data(), bytes.size(), back) && back.total == 3 &&
                  back.revision == 0xdeadbeefcafe1234ull && back.hits.size() == 2 &&
                  back.hits[0].path == L"C:\\Windows" && back.hits[0].name == L"Windows" &&
                  back.hits[0].is_dir && back.hits[0].size == 0 && back.hits[0].mtime == 0 &&
                  back.hits[1].path == L"C:\\data\\big.bin" && !back.hits[1].is_dir &&
                  back.hits[1].size == 0x1234567890abcdefull &&
                  back.hits[1].mtime == 0x0102030405060708ull,
              "search: round-trip with revision");
    }
    {
        // Old host: hits without the trailing revision.
        ipc::PayloadWriter legacy;
        legacy.PutU32(1);
        legacy.PutU32(1);
        legacy.PutString(L"C:\\a.txt");
        legacy.PutString(L"a.txt");
        legacy.PutU32(0);
        legacy.PutU32(5);
        legacy.PutU32(0);
        legacy.PutU32(6);
        legacy.PutU32(0);
        SearchResult back;
        check(wire::ReadSearch(legacy.data().data(), legacy.data().size(), back) && back.total == 1 &&
                  back.revision == 0 && back.hits.size() == 1 && back.hits[0].name == L"a.txt" &&
                  back.hits[0].size == 5 && back.hits[0].mtime == 6,
              "search: payload without revision accepted");
    }
    {
        // A hit must be complete; a cut row is a damaged reply.
        ipc::PayloadWriter truncated;
        truncated.PutU32(0);
        truncated.PutU32(1);
        truncated.PutString(L"p");
        SearchResult back;
        check(!wire::ReadSearch(truncated.data().data(), truncated.data().size(), back),
              "search: truncated hit rejected");
    }

    // --- RSP_IDX_VOLUMES ----------------------------------------------------
    {
        wire::VolumesWire volumes;
        volumes.as_service = true;
        volumes.data_dir = L"D:\\Pulse\\Index";
        VolumeInfo fixed;
        fixed.id = L"C:";
        fixed.label = L"System";
        fixed.mount_point = L"C:\\";
        fixed.file_system = L"NTFS";
        fixed.state = L"ready";
        fixed.error = L"";
        fixed.online = true;
        fixed.supported = true;
        fixed.enabled = true;
        fixed.kind = VolumeKind::Fixed;
        fixed.progress = 100;
        fixed.indexed_items = 0x100000001ull;
        VolumeInfo removable;
        removable.id = L"E:";
        removable.label = L"USB";
        removable.mount_point = L"E:\\";
        removable.file_system = L"exFAT";
        removable.state = L"scanning";
        removable.error = L"";
        removable.online = false;
        removable.supported = true;
        removable.enabled = false;
        removable.kind = VolumeKind::Removable;
        removable.progress = 0;
        removable.indexed_items = 0;
        volumes.volumes = {fixed, removable};
        volumes.excluded_paths = {L"C:\\Temp", L"C:\\Games"};
        volumes.exclude_system = true;
        volumes.system_groups = {L"windows", L"temp"};
        volumes.system_paths = {L"C:\\Windows", L"C:\\Windows\\Temp"};
        const auto bytes = wire::WriteVolumes(volumes);
        ipc::PayloadReader r(bytes.data(), bytes.size());
        uint32_t service = 0, count = 0, flags = 0, kind = 0, progress = 0, lo = 0, hi = 0;
        std::wstring id, label, mount_point, file_system, state, error, data_dir;
        check(r.GetU32(service) && service == 1, "volumes layout: u32 as_service first");
        check(r.GetString(data_dir) && data_dir == L"D:\\Pulse\\Index",
              "volumes layout: string data_dir second");
        check(r.GetU32(count) && count == 2, "volumes layout: u32 volume_count third");
        check(r.GetString(id) && id == L"C:" && r.GetString(label) && label == L"System" &&
                  r.GetString(mount_point) && mount_point == L"C:\\" && r.GetString(file_system) &&
                  file_system == L"NTFS" && r.GetString(state) && state == L"ready" && r.GetString(error) &&
                  error.empty(),
              "volumes layout: volume 0 has six strings in order");
        check(r.GetU32(flags) && flags == 7u && r.GetU32(kind) && kind == 0 && r.GetU32(progress) &&
                  progress == 100 && r.GetU32(lo) && lo == 1 && r.GetU32(hi) && hi == 1,
              "volumes layout: volume 0 packs flags, kind, progress, indexed lo/hi");
        check(r.GetString(id) && id == L"E:" && r.GetString(label) && label == L"USB" &&
                  r.GetString(mount_point) && mount_point == L"E:\\" && r.GetString(file_system) &&
                  file_system == L"exFAT" && r.GetString(state) && state == L"scanning" &&
                  r.GetString(error) && error.empty() && r.GetU32(flags) && flags == 2u &&
                  r.GetU32(kind) && kind == static_cast<uint32_t>(VolumeKind::Removable) &&
                  r.GetU32(progress) && progress == 0 && r.GetU32(lo) && lo == 0 && r.GetU32(hi) && hi == 0,
              "volumes layout: volume 1 flag bits and kind");
        uint32_t excluded_count = 0, system_flags = 0, group_count = 0, system_count = 0;
        check(r.GetU32(excluded_count) && excluded_count == 2, "volumes layout: u32 excluded_count after rows");
        std::wstring path;
        check(r.GetString(path) && path == L"C:\\Temp" && r.GetString(path) && path == L"C:\\Games",
              "volumes layout: excluded paths follow their count");
        check(r.GetU32(system_flags) && system_flags == 1, "volumes layout: u32 system_flags next");
        check(r.GetU32(group_count) && group_count == 2, "volumes layout: u32 group_count next");
        check(r.GetString(path) && path == L"windows" && r.GetString(path) && path == L"temp",
              "volumes layout: system groups follow their count");
        check(r.GetU32(system_count) && system_count == 2, "volumes layout: u32 system_path_count next");
        check(r.GetString(path) && path == L"C:\\Windows" && r.GetString(path) &&
                  path == L"C:\\Windows\\Temp",
              "volumes layout: system paths follow their count");
        check(r.remaining() == 0, "volumes layout: no trailing bytes");
        ipc::PayloadWriter expected;
        expected.PutU32(1);
        expected.PutString(L"D:\\Pulse\\Index");
        expected.PutU32(2);
        expected.PutString(L"C:");
        expected.PutString(L"System");
        expected.PutString(L"C:\\");
        expected.PutString(L"NTFS");
        expected.PutString(L"ready");
        expected.PutString(L"");
        expected.PutU32(7);
        expected.PutU32(0);
        expected.PutU32(100);
        expected.PutU32(1);
        expected.PutU32(1);
        expected.PutString(L"E:");
        expected.PutString(L"USB");
        expected.PutString(L"E:\\");
        expected.PutString(L"exFAT");
        expected.PutString(L"scanning");
        expected.PutString(L"");
        expected.PutU32(2);
        expected.PutU32(1);
        expected.PutU32(0);
        expected.PutU32(0);
        expected.PutU32(0);
        expected.PutU32(2);
        expected.PutString(L"C:\\Temp");
        expected.PutString(L"C:\\Games");
        expected.PutU32(1);
        expected.PutU32(2);
        expected.PutString(L"windows");
        expected.PutString(L"temp");
        expected.PutU32(2);
        expected.PutString(L"C:\\Windows");
        expected.PutString(L"C:\\Windows\\Temp");
        check(bytes == expected.data(), "volumes: writer bytes unchanged");
        wire::VolumesWire back;
        check(wire::ReadVolumes(bytes.data(), bytes.size(), back) && back.as_service &&
                  back.data_dir == L"D:\\Pulse\\Index" && back.volumes.size() == 2 &&
                  back.volumes[0].id == L"C:" && back.volumes[0].online && back.volumes[0].supported &&
                  back.volumes[0].enabled && back.volumes[0].kind == VolumeKind::Fixed &&
                  back.volumes[0].progress == 100 && back.volumes[0].indexed_items == 0x100000001ull &&
                  back.volumes[1].id == L"E:" && !back.volumes[1].online && back.volumes[1].supported &&
                  !back.volumes[1].enabled && back.volumes[1].kind == VolumeKind::Removable &&
                  back.excluded_paths.size() == 2 && back.excluded_paths[1] == L"C:\\Games" &&
                  back.system_known && back.exclude_system && back.system_groups.size() == 2 &&
                  back.system_paths.size() == 2 && back.system_paths[1] == L"C:\\Windows\\Temp",
              "volumes: service round-trip");
        volumes.exclude_system = false;
        const auto without_system = wire::WriteVolumes(volumes);
        check(wire::ReadVolumes(without_system.data(), without_system.size(), back) && back.system_known &&
                  !back.exclude_system,
              "volumes: system switch off still known");
    }
    {
        // Session mode: rows, then one zero count and nothing else.
        wire::VolumesWire volumes;
        volumes.as_service = false;
        volumes.data_dir = L"D:\\Pulse\\User";
        volumes.excluded_paths = {L"ignored in session mode"};
        const auto bytes = wire::WriteVolumes(volumes);
        ipc::PayloadReader r(bytes.data(), bytes.size());
        uint32_t service = 0, count = 0, tail = 0;
        std::wstring data_dir;
        check(r.GetU32(service) && service == 0 && r.GetString(data_dir) &&
                  data_dir == L"D:\\Pulse\\User" && r.GetU32(count) && count == 0 && r.GetU32(tail) &&
                  tail == 0 && r.remaining() == 0,
              "volumes: session mode writes a single zero tail");
        wire::VolumesWire back;
        check(wire::ReadVolumes(bytes.data(), bytes.size(), back) && !back.as_service &&
                  back.volumes.empty() && back.excluded_paths.empty() && !back.system_known &&
                  !back.exclude_system,
              "volumes: session mode round-trip");
    }
    {
        // Old service host: volume rows and nothing after them.
        ipc::PayloadWriter legacy;
        legacy.PutU32(1);
        legacy.PutString(L"D:\\Index");
        legacy.PutU32(0);
        wire::VolumesWire back;
        check(wire::ReadVolumes(legacy.data().data(), legacy.data().size(), back) && back.as_service &&
                  back.data_dir == L"D:\\Index" && back.excluded_paths.empty() && !back.system_known &&
                  back.system_groups.empty() && back.system_paths.empty(),
              "volumes: old payload without the scope tail accepted");
    }
    {
        // Half an exclusion list: keep the paths that arrived.
        ipc::PayloadWriter cut;
        cut.PutU32(1);
        cut.PutString(L"D:\\Index");
        cut.PutU32(0);
        cut.PutU32(2);
        cut.PutString(L"C:\\Temp");
        wire::VolumesWire back;
        check(wire::ReadVolumes(cut.data().data(), cut.data().size(), back) &&
                  back.excluded_paths.size() == 1 && back.excluded_paths[0] == L"C:\\Temp" &&
                  !back.system_known,
              "volumes: truncated exclusion list keeps what arrived");
    }
    {
        // Cut system section: report unknown and drop the partial lists.
        ipc::PayloadWriter cut;
        cut.PutU32(1);
        cut.PutString(L"D:\\Index");
        cut.PutU32(0);
        cut.PutU32(0);
        cut.PutU32(0);
        cut.PutU32(2);
        cut.PutString(L"windows");
        wire::VolumesWire back;
        check(wire::ReadVolumes(cut.data().data(), cut.data().size(), back) && !back.system_known &&
                  !back.exclude_system && back.system_groups.empty() && back.system_paths.empty(),
              "volumes: truncated system section reports unknown");
    }
    {
        // Required rows missing: reject the whole message.
        ipc::PayloadWriter truncated;
        truncated.PutU32(0);
        truncated.PutString(L"D:\\Index");
        truncated.PutU32(1);
        truncated.PutString(L"C:");
        wire::VolumesWire back;
        check(!wire::ReadVolumes(truncated.data().data(), truncated.data().size(), back),
              "volumes: truncated volume row rejected");
        ipc::PayloadWriter no_dir;
        no_dir.PutU32(0);
        check(!wire::ReadVolumes(no_dir.data().data(), no_dir.data().size(), back),
              "volumes: missing data_dir rejected");
    }

    return failures ? 1 : 0;
}
