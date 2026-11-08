// details_info.cpp — see details_info.h.
#include "details_info.h"
#include "../common/display_path.h"
#include "../common/localization.h"
#include "../common/text_format.h"
#include "../fs/fs_enum.h"
#include "system_info.h"
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

std::wstring ItemsText(uint64_t entries) {
    wchar_t text[64]{};
    swprintf_s(text, l10n::Get(I::ItemsCountFormat).c_str(), static_cast<int>(entries));
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

    if (input.this_pc) {
        const SystemFacts& facts = Facts();
        view.name = l10n::Get(I::ThisPc);
        view.subtitle_text = facts.os_name;
        view.context_rows.emplace_back(l10n::Get(I::AboutOs), facts.os_name);
        view.context_rows.emplace_back(l10n::Get(I::DetailsSysVersion), facts.os_build);
        view.context_rows.emplace_back(l10n::Get(I::DetailsComputerName), facts.computer);
        view.context_rows.emplace_back(l10n::Get(I::DetailsProcessor), facts.cpu);
        view.context_rows.emplace_back(l10n::Get(I::DetailsMemory), facts.memory);
        return;
    }

    view.is_dir = true;
    view.path = input.folder_path;
    view.name = LeafName(input.folder_path);
    view.type_text = l10n::Get(I::KindFolder);
    view.location_text = path::FriendlyPathText(input.folder_path);
    view.context_rows.emplace_back(l10n::Get(I::Location), view.location_text);
    view.context_rows.emplace_back(l10n::Get(I::ColumnSize),
        input.size_bytes ? pulse::format::ByteSize(*input.size_bytes, true)
                         : std::wstring(L"\u2014"));
    view.context_rows.emplace_back(l10n::Get(I::Contains), ItemsText(input.entries));
}

} // namespace pulse::app
