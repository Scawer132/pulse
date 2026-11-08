// details_info.h — The details panel card shown while nothing is selected: the
// current folder, or the machine when the tab sits at 此电脑. Facts only, so
// the card is filled without reading the folder or the selection again.
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
    std::wstring folder_path;             // current directory for a folder card
    std::optional<uint64_t> entries;      // children the listing already knows
    std::optional<uint64_t> size_bytes;   // cached walk result, when one exists
};

// Fills @p view with a context card. Callers must not lay out or draw the
// card's action buttons, attribute toggles or security rows: it has none.
void FillDetailsInfo(const DetailsInfoInput& input, ui::DetailsPanelView& view);

} // namespace pulse::app
