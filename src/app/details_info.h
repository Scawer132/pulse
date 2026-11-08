// details_info.h — What the details panel shows while nothing is selected: the
// current folder, described the same way a selected folder is, or the machine
// at 此电脑. Both only set the panel's subject; the sections draw the facts.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "../ui/ui_view_model.h"

namespace pulse::app {

// Virtual views (搜索 / 最近 / 回收站 / 网络 / 标签) keep the empty-state
// illustration: their "folder" is a query, not a place on disk.
bool DetailsInfoAppliesTo(const std::wstring& path);

struct DetailsInfoInput {
    bool this_pc = false;
    std::wstring folder_path;   // current directory for a folder card
};

// Fills @p view with a context card. Callers must not lay out or draw the
// card's action buttons, name-row toggles or its interactive sections: nothing
// is selected, so those would target the folder the tab already shows.
void FillDetailsInfo(const DetailsInfoInput& input, ui::DetailsPanelView& view);

} // namespace pulse::app
