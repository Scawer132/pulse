// details_info.cpp — see details_info.h.
#include "details_info.h"
#include "../common/localization.h"
#include "../common/text_format.h"
#include "../fs/fs_enum.h"
#include "system_info.h"
#include <cmath>
#include <cstdio>

namespace pulse::app {
namespace {
using I = l10n::StringId;

// "C:\" reads as "C:"; every other path as its last component.
std::wstring LeafName(std::wstring path) {
    while (path.size() > 1 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    if (path.size() == 2 && path[1] == L':') return path;
    const size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? path : path.substr(at + 1);
}

// "1920 × 1080 @ 144 Hz": the primary display as it runs now.
std::wstring ResolutionText(const SystemFacts& facts) {
    wchar_t text[64]{};
    swprintf_s(text, L"%d × %d", facts.screen_width, facts.screen_height);
    std::wstring value = text;
    if (facts.refresh_hz > 0) value += L" @ " + std::to_wstring(facts.refresh_hz) + L" Hz";
    return value;
}

// The CPU line keeps its nominal clock the way fastfetch prints it; the cores
// row stays short enough to fit the panel's value column.
std::wstring CpuText(const SystemFacts& facts) {
    return facts.cpu_clock.empty() ? facts.cpu : facts.cpu + L" @ " + facts.cpu_clock;
}

std::wstring CoresText(const SystemFacts& facts) {
    wchar_t text[96]{};
    swprintf_s(text, l10n::Get(I::DetailsCoresFormat).c_str(),
               static_cast<unsigned long long>(facts.cpu_physical),
               static_cast<unsigned long long>(facts.cpu_logical));
    return text;
}

} // namespace

bool DetailsInfoAppliesTo(const std::wstring& path) {
    return !path.empty() && !fs::IsVirtualPath(path);
}

void FillDetailsInfo(const DetailsInfoInput& input, ui::DetailsPanelView& view) {
    // One layout path for cards and selections: the panel's early "no
    // selection" branch would otherwise paint the empty-state illustration.
    view.has_selection = true;
    view.context = true;
    view.pc_card = input.this_pc;
    view.context_rows.clear();
    view.subtitle_text.clear();
    view.pc_meta.clear();

    if (input.this_pc) {
        const SystemFacts& facts = Facts();
        view.name = l10n::Get(I::ThisPc);
        // The OS name heads the card the way Explorer labels This PC; the rows
        // below carry what only a system card shows.
        view.subtitle_text = facts.os_name;
        view.pc_meta = facts.computer;
        if (!facts.os_arch.empty())
            view.pc_meta += (view.pc_meta.empty() ? L"" : L" · ") + facts.os_arch;

        // Ordered like a system summary: OS, machine, then what it runs on.
        if (!facts.os_build.empty())
            view.context_rows.emplace_back(l10n::Get(I::DetailsSysVersion), facts.os_build);
        if (!facts.model.empty())
            view.context_rows.emplace_back(l10n::Get(I::DetailsModel), facts.model);
        if (!facts.firmware.empty())
            view.context_rows.emplace_back(l10n::Get(I::DetailsFirmware), facts.firmware);
        view.context_rows.emplace_back(l10n::Get(I::DetailsUptime), UptimeText());
        if (facts.screen_width > 0 && facts.screen_height > 0)
            view.context_rows.emplace_back(l10n::Get(I::DetailsResolution), ResolutionText(facts));
        if (!facts.cpu.empty())
            view.context_rows.emplace_back(l10n::Get(I::DetailsProcessor), CpuText(facts));
        if (facts.cpu_physical > 0 && facts.cpu_logical > 0)
            view.context_rows.emplace_back(l10n::Get(I::DetailsCores), CoresText(facts));
        if (!facts.gpu.empty())
            view.context_rows.emplace_back(l10n::Get(I::DetailsGraphics), facts.gpu);
        if (facts.memory_total > 0) {
            std::wstring memory = format::ByteSize(facts.memory_used, true);
            if (!memory.empty()) memory += L" / ";
            memory += facts.memory + L" (" + std::to_wstring(facts.memory_percent) + L"%)";
            view.context_rows.emplace_back(l10n::Get(I::DetailsMemory), memory);
        }
        if (!facts.system_drive.empty()) {
            std::wstring disk = facts.system_drive;
            if (facts.system_total > 0) {
                const uint64_t used = facts.system_total - facts.system_free;
                const int percent = static_cast<int>(std::lround(
                    100.0 * static_cast<double>(used) / static_cast<double>(facts.system_total)));
                disk += L" " + format::ByteSize(used, true) + L" / " +
                        format::ByteSize(facts.system_total, true) + L" (" +
                        std::to_wstring(percent) + L"%)";
            }
            view.context_rows.emplace_back(l10n::Get(I::DetailsSystemDrive), disk);
        }
        return;
    }

    // A folder card is the folder as a selected folder would be: name, path and
    // is_dir here, with 位置/类型/大小/包含/日期 and 其他 filled from the same
    // meta and size machinery the panel already uses.
    view.multi_count = 1;
    view.is_dir = true;
    view.path = input.folder_path;
    view.name = LeafName(input.folder_path);
}

} // namespace pulse::app
