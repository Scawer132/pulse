// details_info.cpp — see details_info.h.
#include "details_info.h"
#include "../common/localization.h"
#include "../fs/fs_enum.h"
#include "system_info.h"

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

    if (input.this_pc) {
        const SystemFacts& facts = Facts();
        view.name = l10n::Get(I::ThisPc);
        // The OS name heads the card the way Explorer labels This PC; the rows
        // below carry what only a system card shows.
        view.subtitle_text = facts.os_name;
        view.context_rows.emplace_back(l10n::Get(I::DetailsSysVersion), facts.os_build);
        view.context_rows.emplace_back(l10n::Get(I::DetailsComputerName), facts.computer);
        view.context_rows.emplace_back(l10n::Get(I::DetailsProcessor), facts.cpu);
        view.context_rows.emplace_back(l10n::Get(I::DetailsMemory), facts.memory);
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
