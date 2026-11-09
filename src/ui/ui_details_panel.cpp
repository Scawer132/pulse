// ui_details_panel.cpp — Details panel draw and content height.
#include "ui_renderer.h"
#include "../fs/cloud_state.h"
#include "preview_footer_layout.h"
#include "ui_renderer_internal.h"
#include "../common/localization.h"
#include "tab_shape.h"
#include "bloom_accent_picker.h"
#include "typography.h"
#include "../app/resource.h"
#include "../app/places.h"
#include "../common/text_format.h"
#include <windowsx.h>
#include <d2d1effects.h>
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <string_view>
#include <unordered_map>

namespace pulse::ui {

namespace {
// Details motion (animation phase 7b, pulse_motion_v2): when the selection
// moves to another item, the name, buttons and properties are revealed top
// to bottom - a soft edge sweeps down the panel while the content rises
// 6 dip - so the panel reads as new instead of flickering in place. Held
// arrow keys (changes under 160 ms apart) just swap. The pinned preview well
// above (it may hold a preview-handler window) does not move. State per
// renderer lives here; frames come from the renderer's frame pump
// (RequestMotionFrame), asked for only during the sweep.
constexpr float kDetailsSweepMs = 260.0f, kDetailsSweepBandDip = 56.0f, kDetailsRiseDip = 6.0f;
constexpr uint64_t kDetailsRapidMs = 160;

struct DetailsMotion {
    std::wstring path;
    bool seen = false;
    uint64_t last_change = 0;  // NowMs of the last selection change
    uint64_t sweep_at = 0;     // 0: no sweep running
    bool animations = true;
    uint64_t animations_checked = 0;
};

DetailsMotion& DetailsMotionFor(const void* renderer) {
    static std::unordered_map<const void*, DetailsMotion> motions;
    return motions[renderer];
}

// One line of details text broken at the last space that fits; prefers a
// balanced second line over a lone orphan word ("... Laptop" / "GPU"). Returns
// {text, ""} when it already fits. Used by the 此电脑 card's identity block and
// by fact values that are too long for their column.
std::pair<std::wstring, std::wstring> SplitDetailLine(const std::wstring& text, float width,
                                                      IDWriteFactory2* factory,
                                                      IDWriteTextFormat* fmt) {
    std::pair<std::wstring, std::wstring> out{text, std::wstring()};
    if (!factory || !fmt || text.empty() || width <= 1.0f) return out;
    const auto measure = [&](const std::wstring& value) {
        return MeasureTextWidth(factory, fmt, value);
    };
    if (measure(text) <= width) return out;
    size_t cut = text.find_last_of(L' ');
    while (cut != std::wstring::npos && cut > 0 && measure(text.substr(0, cut)) > width)
        cut = cut > 1 ? text.find_last_of(L' ', cut - 1) : std::wstring::npos;
    if (cut == std::wstring::npos || cut == 0) {
        // No usable space (CJK, a hyphenated model name): cut by character.
        size_t lo = 1, hi = text.size();
        while (lo < hi) {
            const size_t mid = (lo + hi + 1) / 2;
            if (measure(text.substr(0, mid)) <= width) lo = mid; else hi = mid - 1;
        }
        cut = lo;
    } else {
        const std::wstring head = text.substr(0, cut);
        const std::wstring tail = cut + 1 < text.size() ? text.substr(cut + 1) : std::wstring();
        const size_t prev = head.find_last_of(L' ');
        if (!tail.empty() && tail.find(L' ') == std::wstring::npos &&
            prev != std::wstring::npos && prev > 0 &&
            measure(tail) * 2.5f < measure(head) && measure(text.substr(prev)) <= width)
            cut = prev;
    }
    std::wstring second = text.substr(cut);
    while (!second.empty() && second.front() == L' ') second.erase(second.begin());
    out.first = text.substr(0, cut);
    while (!out.first.empty() && out.first.back() == L' ') out.first.pop_back();
    out.second = std::move(second);
    return out;
}
}  // namespace

D2D1_RECT_F MainRenderer::DetailsPanelRect(float w, float h) const {
    if (DetailsPanelWidth(w) <= 0.0f) return D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
    const float pw = details_width_ * scale_;
    const float top = title_bar_height_ + toolbar_height_ + margin_;
    const float bottom = h - status_height_ - margin_;
    return D2D1::RectF(w - margin_ - pw, top, w - margin_, bottom);
}

void MainRenderer::ReleasePreviewHosts(bool details, bool hover) {
    // The decode cache owns the Pulse.Preview.exe children behind the details
    // well and the hover card; releasing them is what makes the switch mean
    // "no preview process", not just "no new requests". The well's content
    // itself belongs to the embedded preview host, which the app closes.
    if (details) {
        details_preview_bounds_ = {};
        details_cache_.StopHosts();
    }
    if (hover) hover_cache_.StopHosts();
}
void MainRenderer::DrawDetailsPanel(const WindowViewModel& vm, const D2D1_RECT_F& rect,
                                    const Theme& theme) {
    details_preview_bounds_ = {};
    const D2D1_RECT_F panel = DetailsPanelRect(rect.right, rect.bottom);
    if (panel.right - panel.left <= 1.0f || !compositor_ || !compositor_->Dc()) return;
    ID2D1DeviceContext* dc = compositor_->Dc();
    const float s = scale_;
    const DetailsPanelView& d = vm.details;
    // Preview off => zero-height well: the panel starts at the name row.
    const float previewH = DetailsPanelPreviewHeight(d, panel, s);
    DetailsHitRects hit;
    LayoutDetailsPanel(panel, s, d, compositor_->DwriteFactory(),
                       compositor_->SmallFormat(), compositor_, previewH, hit);

    DetailsMotion& dm = DetailsMotionFor(this);
    if (motion_now_ - dm.animations_checked > 2000 || !dm.animations_checked) {
        dm.animations_checked = motion_now_ ? motion_now_ : 1;
        dm.animations = motion::SystemAnimationsEnabled();
    }
    if (d.has_selection && (!dm.seen || d.path != dm.path)) {
        const bool rapid = dm.last_change && motion_now_ - dm.last_change < kDetailsRapidMs;
        dm.sweep_at = dm.seen && !rapid && dm.animations && !IsHighContrast() ? motion_now_ : 0;
        dm.seen = true;
        dm.path = d.path;
        dm.last_change = motion_now_;
    } else if (!d.has_selection && dm.seen) {
        dm.path.clear();
        dm.sweep_at = 0;
    }
    // The inspector stays an independent right-hand card, with its own scroll
    // and splitter. File-pane opacity must not fade these controls.
    const float cardRadius = theme.radius_control * s;
    // Share the sidebar sheet so both sides retain the same transparency.
    MakeBrush(dc, theme.stroke_card, brStrokeCard_);
    dc->DrawRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(panel.left + 0.5f * s, panel.top + 0.5f * s,
                    panel.right - 0.5f * s, panel.bottom - 0.5f * s),
        cardRadius, cardRadius), brStrokeCard_.get(), 1.0f * s);

    fluent::SplitterSpec resizeSplitter;
    resizeSplitter.bounds = D2D1::RectF(panel.left - 4.0f * s, panel.top,
                                        panel.left + 4.0f * s, panel.bottom);
    resizeSplitter.vertical = true;
    resizeSplitter.state.hovered =
        vm.hover_region == static_cast<int>(HitTestResult::DetailsResize);
    resizeSplitter.state.pressed = vm.details_resize_pressed;
    painter_.DrawSplitter(resizeSplitter);

    auto text = [&](const std::wstring& str, const D2D1_RECT_F& rc,
                    IDWriteTextFormat* fmt, const D2D1_COLOR_F& color,
                    bool right = false) {
        MakeBrush(dc, color, brText_);
        if (!fmt) return;
        const auto old = fmt->GetTextAlignment();
        fmt->SetTextAlignment(right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                    : DWRITE_TEXT_ALIGNMENT_LEADING);
        DrawTextRect(dc, fmt, brText_.get(), str, rc.left, rc.top,
                     rc.right - rc.left, rc.bottom - rc.top);
        fmt->SetTextAlignment(old);
    };
    auto section = [&](const wchar_t* label, float y, int id) {
        const bool hovered = IsHovered(vm, HitTestResult::DetailsSection, id);
        if (hovered) {
            MakeBrush(dc, theme.fill_hover, brFillHover_);
            FillRoundedRect(dc, brFillHover_.get(), panel.left + 6.0f * s, y,
                            panel.right - panel.left - 12.0f * s, 28.0f * s, 6.0f * s);
        }
        // Vector chevron: icon fonts on some machines lack the right-chevron glyph.
        const bool collapsed = (d.collapsed_mask >> id) & 1u;
        MakeBrush(dc, theme.text_secondary, brText_);
        const float cx = panel.left + 17.0f * s;
        const float cy = y + 14.0f * s;
        const float t = 1.3f * s;
        if (collapsed) {
            dc->DrawLine(D2D1::Point2F(cx - 2.0f * s, cy - 4.0f * s),
                         D2D1::Point2F(cx + 2.0f * s, cy), brText_.get(), t);
            dc->DrawLine(D2D1::Point2F(cx + 2.0f * s, cy),
                         D2D1::Point2F(cx - 2.0f * s, cy + 4.0f * s), brText_.get(), t);
        } else {
            dc->DrawLine(D2D1::Point2F(cx - 4.0f * s, cy - 2.0f * s),
                         D2D1::Point2F(cx, cy + 2.0f * s), brText_.get(), t);
            dc->DrawLine(D2D1::Point2F(cx, cy + 2.0f * s),
                         D2D1::Point2F(cx + 4.0f * s, cy - 2.0f * s), brText_.get(), t);
        }
        text(label, D2D1::RectF(panel.left + 28.0f * s, y + 4.0f * s,
                                panel.right - 12.0f * s, y + 24.0f * s),
             compositor_->SmallFormat(), theme.accent);
    };
    auto centeredText = [&](const std::wstring& str, const D2D1_RECT_F& rc,
                            IDWriteTextFormat* fmt, const D2D1_COLOR_F& color) {
        if (!fmt) return;
        MakeBrush(dc, color, brText_);
        const auto oldAlignment = fmt->GetTextAlignment();
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        DrawTextRect(dc, fmt, brText_.get(), str, rc.left, rc.top,
                     rc.right - rc.left, rc.bottom - rc.top);
        fmt->SetTextAlignment(oldAlignment);
    };
    auto ghostButton = [&](const D2D1_RECT_F& rc, const wchar_t* glyph,
                           const std::wstring& label, bool hovered) {
        if (hovered) {
            MakeBrush(dc, theme.fill_hover, brFillHover_);
            FillRoundedRect(dc, brFillHover_.get(), rc.left, rc.top,
                            rc.right - rc.left, rc.bottom - rc.top, 6.0f * s);
        }
        MakeBrush(dc, theme.stroke_card, brStrokeCard_);
        dc->DrawRoundedRectangle(D2D1::RoundedRect(rc, 6.0f * s, 6.0f * s),
                                 brStrokeCard_.get(), 1.0f);
        if (glyph) DrawIconText(rc.left + 6.0f * s, rc.top, 20.0f * s,
                                rc.bottom - rc.top, glyph, L"?", theme.text_secondary, 0.72f);
        text(label, D2D1::RectF(rc.left + 28.0f * s, rc.top, rc.right - 4.0f * s, rc.bottom),
             compositor_->SmallFormat(), theme.text);
    };

    if (!d.has_selection) {
        const D2D1_RECT_F content = D2D1::RectF(
            panel.left + 16.0f * s, panel.top + 16.0f * s,
            panel.right - 16.0f * s, panel.bottom - 16.0f * s);
        const float availableWidth = std::max(0.0f, content.right - content.left);
        const float artWidth = std::min(180.0f * s, availableWidth);
        const float artHeight = artWidth * 240.0f / 320.0f;
        const float titleHeight = 24.0f * s;
        const float gap = 2.0f * s;
        const float totalHeight = artHeight + gap + titleHeight;
        const float top = content.top + std::max(
            0.0f, ((content.bottom - content.top) - totalHeight) * 0.5f);
        const D2D1_RECT_F art = D2D1::RectF(
            (content.left + content.right - artWidth) * 0.5f, top,
            (content.left + content.right + artWidth) * 0.5f, top + artHeight);
        const float svgOpacity = theme.bg.r > 0.5f ? 0.68f : 1.0f;
        if (!DrawNoSelectionSvg(art, svgOpacity)) {
            fluent::EmptyStateSpec empty;
            empty.bounds = content;
            empty.glyph = kIconFile;
            empty.title = pulse::l10n::Get(pulse::l10n::StringId::DetailsNoSelection);
            painter_.DrawEmptyState(empty);
            return;
        }
        centeredText(pulse::l10n::Get(pulse::l10n::StringId::DetailsNoSelection),
            D2D1::RectF(content.left, art.bottom + gap,
                        content.right, art.bottom + gap + titleHeight),
            compositor_->HeaderFormat(), theme.text_secondary);
        return;
    }

    const float pad = 12.0f * s;
    dc->PushAxisAlignedClip(panel, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float previewTop = panel.top + pad;
    float y = previewTop;

    // Content and playback are rendered by the same embedded host as Space.
    // This renderer owns only the panel layout and metadata beneath it.
    if (d.preview_enabled && previewH > 0.0f) {
        const auto preview = D2D1::RectF(panel.left + pad, previewTop,
            panel.right - pad, previewTop + previewH);
        const D2D1_RECT_F contentRc = D2D1::RectF(preview.left + 2.0f * s, preview.top + 2.0f * s,
            preview.right - 2.0f * s, preview.bottom - 2.0f * s);
        if (d.pc_card) {
            // 此电脑: the well is the card's header, not a preview box. The OS
            // logo on the left, the machine's identity beside it, facts below.
            // It comes before the icon-only and selection branches because a
            // system card has no path to preview and must never inherit one.
            const float logo = 64.0f * s;
            const float logoX = contentRc.left + 6.0f * s;
            const float logoTop = contentRc.top +
                ((contentRc.bottom - contentRc.top) - logo) * 0.5f;
            const D2D1_RECT_F logoRc = D2D1::RectF(logoX, logoTop, logoX + logo, logoTop + logo);
            if (!DrawFluentSvg(IDR_WINDOWS_OS_SVG, logoRc, 1.0f, nullptr, true)) {
                // Missing resource or no D2D SVG support: keep a recognizable glyph.
                if (!DrawFluentSvg(IDR_FLUENT_PC_SVG, logoRc, 1.0f, nullptr, true))
                    DrawIconText(logoRc.left, logoRc.top, logo, logo, L"\xE977", L"PC",
                                 theme.text_secondary, 1.0f);
            }
            const float textX = logoX + logo + 14.0f * s;
            const float textW = (std::max)(24.0f * s, contentRc.right - textX);
            // Boxes follow the text size, so a larger interface font (125/150%)
            // still fits the three lines inside the band.
            IDWriteTextFormat* header_fmt = compositor_->HeaderFormat();
            IDWriteTextFormat* small_fmt_h = compositor_->SmallFormat();
            const float title_h = (std::max)(20.0f * s,
                header_fmt ? header_fmt->GetFontSize() * 1.30f : 20.0f * s);
            const float line_h = (std::max)(18.0f * s,
                small_fmt_h ? small_fmt_h->GetFontSize() * 1.40f : 18.0f * s);
            const float gap1 = 5.0f * s, gap2 = 3.0f * s;
            // A long OS name wraps inside the identity block instead of ending
            // in an ellipsis on narrow panels and in every UI language.
            const auto os_text = SplitDetailLine(d.subtitle_text, textW,
                                                 compositor_->DwriteFactory(), small_fmt_h);
            const float os_h = (os_text.second.empty() ? 1.0f : 2.0f) * line_h;
            const float blockH = title_h + gap1 + os_h + gap2 + line_h;
            const float textY = logoTop + (logo - blockH) * 0.5f;
            text(d.name, D2D1::RectF(textX, textY, textX + textW, textY + title_h),
                 header_fmt, theme.text);
            const float osTop = textY + title_h + gap1;
            MakeBrush(dc, theme.text, brText_);
            if (os_text.second.empty()) {
                DrawTextEndEllipsis(compositor_, small_fmt_h, brText_.get(), d.subtitle_text,
                                    textX, osTop, textW, line_h);
            } else {
                DrawTextEndEllipsis(compositor_, small_fmt_h, brText_.get(), os_text.first,
                                    textX, osTop, textW, line_h);
                DrawTextEndEllipsis(compositor_, small_fmt_h, brText_.get(), os_text.second,
                                    textX, osTop + line_h, textW, line_h);
            }
            MakeBrush(dc, theme.text_secondary, brTextSecondary_);
            DrawTextEndEllipsis(compositor_, small_fmt_h, brTextSecondary_.get(),
                                d.pc_meta, textX, osTop + os_h + gap2, textW, line_h);
        } else if (d.preview_icon_only || (d.context && d.multi_count <= 1)) {
            // 启用预览 › 详情面板预览 off: the well keeps the file-type icon the
            // way a folder's preview does, and the file itself is never read.
            // A folder card also lands here: the embedded host needs a selected
            // row, so without the icon its well would stay an empty box.
            const float icon = 58.0f * s;
            const float icon_cx = (contentRc.left + contentRc.right) * 0.5f;
            const float icon_top = contentRc.top +
                ((contentRc.bottom - contentRc.top) - icon) * 0.5f;
            const D2D1_RECT_F iconRc = D2D1::RectF(icon_cx - icon * 0.5f, icon_top,
                icon_cx + icon * 0.5f, icon_top + icon);
            const float hidden_alpha = HiddenEntryAlpha(d.attrs);
            if (ID2D1Bitmap* bmp = icon_cache_.BitmapFor(d.path, d.name, d.is_dir, d.attrs, icon)) {
                dc->DrawBitmap(bmp, &iconRc, hidden_alpha,
                               D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            } else if (d.is_dir) {
                DrawFolderIcon(iconRc.left, iconRc.top, icon, theme, hidden_alpha);
            } else {
                DrawFileIcon(iconRc.left, iconRc.top, icon, theme, hidden_alpha);
            }
            if (d.is_link)
                DrawLinkOverlay(iconRc.left, iconRc.top, icon, theme, hidden_alpha);
            if (IsProtectedSystemEntry(d.attrs)) DrawProtectedBadge(dc, iconRc, scale_, theme);
        } else if (d.cloud_online && d.multi_count <= 1 && !d.is_dir) {
            // 下载并预览 (C4b): the content is only in the cloud, so the well
            // explains what is missing and offers the one button that fetches
            // it; a progress bar takes its place meanwhile. The embedded host
            // is never asked for this file (no bounds are reserved for it).
            IDWriteFactory2* dw = compositor_->DwriteFactory();
            IDWriteTextFormat* fmt = compositor_->SmallFormat();
            const std::wstring provider = d.cloud_provider.empty()
                ? pulse::l10n::Get(pulse::l10n::StringId::CloudProviderGeneric) : d.cloud_provider;
            const auto fill = [](std::wstring str, std::wstring_view key, const std::wstring& value) {
                if (const size_t at = str.find(key); at != std::wstring::npos) str.replace(at, key.size(), value);
                return str;
            };
            const bool downloading = d.cloud_download_percent >= 0;
            const std::wstring note = downloading
                ? fill(fill(pulse::l10n::Get(pulse::l10n::StringId::CloudDownloadingFrom), L"{provider}", provider),
                       L"{n}", std::to_wstring(d.cloud_download_percent))
                : fill(fill(pulse::l10n::Get(pulse::l10n::StringId::CloudOnlyNote), L"{size}",
                            pulse::format::ByteSize(d.size_value, true)), L"{provider}", provider);
            const bool button = !downloading && hit.cloud_download.right > hit.cloud_download.left;
            const float bottom = button ? hit.cloud_download.top - 12.0f * s : contentRc.bottom;
            const float textW = (std::max)(40.0f * s, (contentRc.right - contentRc.left) - 32.0f * s);
            const float cx = (contentRc.left + contentRc.right) * 0.5f;
            ComPtr<IDWriteTextLayout> noteLayout;
            if (dw && fmt)
                dw->CreateTextLayout(note.data(), static_cast<UINT32>(note.size()), fmt, textW, 400.0f * s,
                                     &noteLayout);
            float textH = 18.0f * s;
            if (noteLayout.get()) {
                noteLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                noteLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                noteLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                DWRITE_TEXT_METRICS metrics{};
                if (SUCCEEDED(noteLayout->GetMetrics(&metrics))) textH = metrics.height;
            }
            const float iconH = 30.0f * s, gap = 10.0f * s, barH = 4.0f * s;
            const float group = iconH + gap + textH + (downloading ? gap + barH : 0.0f);
            const float top = contentRc.top + (std::max)(8.0f * s, ((bottom - contentRc.top) - group) * 0.5f);
            DrawIconText(contentRc.left, top, contentRc.right - contentRc.left, iconH,
                         downloading ? L"\xE896" : L"\xE753", downloading ? L"\x2193" : L"\x2601",
                         theme.text_secondary, 1.0f);
            if (noteLayout.get() && bottom > top + iconH + gap) {
                dc->PushAxisAlignedClip(D2D1::RectF(contentRc.left, contentRc.top, contentRc.right, bottom),
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                MakeBrush(dc, theme.text_secondary, brText_);
                dc->DrawTextLayout(D2D1::Point2F(cx - textW * 0.5f, top + iconH + gap), noteLayout.get(),
                                   brText_.get());
                dc->PopAxisAlignedClip();
            }
            if (downloading) {
                const float barW = (std::min)(220.0f * s, textW);
                const float by = top + iconH + gap + textH + gap;
                MakeBrush(dc, theme.fill_hover, brFillHover_);
                FillRoundedRect(dc, brFillHover_.get(), cx - barW * 0.5f, by, barW, barH, barH * 0.5f);
                const float done = barW * std::clamp(static_cast<float>(d.cloud_download_percent) / 100.0f, 0.0f, 1.0f);
                if (done >= barH) {
                    MakeBrush(dc, theme.accent, brAccent_);
                    FillRoundedRect(dc, brAccent_.get(), cx - barW * 0.5f, by, done, barH, barH * 0.5f);
                }
            } else if (button) {
                const D2D1_RECT_F& rc = hit.cloud_download;
                const bool hot = IsHovered(vm, HitTestResult::DetailsCloudDownload);
                MakeBrush(dc, hot ? theme.accent_hover : theme.accent, brAccent_);
                FillRoundedRect(dc, brAccent_.get(), rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 5.0f * s);
                const std::wstring label = pulse::l10n::Get(pulse::l10n::StringId::CloudDownloadPreview);
                const float glyphW = 16.0f * s, glyphGap = 6.0f * s;
                const float labelW = dw && fmt ? MeasureLayoutText(compositor_, dw, fmt, label) : 0.0f;
                const float startX = (std::max)(rc.left + 8.0f * s,
                                              (rc.left + rc.right - (glyphW + glyphGap + labelW)) * 0.5f);
                DrawIconText(startX, rc.top, glyphW, rc.bottom - rc.top, L"\xE896", L"\x2193",
                             theme.accent_text, 1.0f);
                ComPtr<IDWriteTextLayout> labelLayout;
                const float labelBox = (std::max)(1.0f, rc.right - 8.0f * s - (startX + glyphW + glyphGap));
                if (dw && fmt)
                    dw->CreateTextLayout(label.data(), static_cast<UINT32>(label.size()), fmt, labelBox,
                                         rc.bottom - rc.top, &labelLayout);
                if (labelLayout.get()) {
                    labelLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    labelLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    labelLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    MakeBrush(dc, theme.accent_text, brText_);
                    dc->DrawTextLayout(D2D1::Point2F(startX + glyphW + glyphGap, rc.top), labelLayout.get(),
                                       brText_.get());
                }
            }
        } else if (d.multi_count <= 1) {
            details_preview_bounds_ = contentRc;
            if (!d.is_dir) {
                std::vector<PreviewProperty> ignored;
                details_cache_.Properties(d.path, d.attrs, d.view_generation,
                    d.modified_value, d.size_value, ignored);
            }
        } else {
            wchar_t count[64]{};
            swprintf_s(count, l10n::Get(l10n::StringId::ItemsCountFormat).c_str(), d.multi_count);
            centeredText(count, preview, compositor_->SmallFormat(), theme.text_secondary);
        }
    }
    const auto toggle = hit.preview_toggle;
    if (IsHovered(vm, HitTestResult::DetailsPreviewToggle)) {
        MakeBrush(dc, theme.fill_hover, brFillHover_);
        FillRoundedRect(dc, brFillHover_.get(), toggle.left, toggle.top,
            toggle.right - toggle.left, toggle.bottom - toggle.top, 4.0f * s);
    }
    MakeBrush(dc, theme.stroke_card, brStrokeCard_);
    dc->DrawLine(D2D1::Point2F(toggle.left, toggle.top),
                 D2D1::Point2F(toggle.right, toggle.top), brStrokeCard_.get(), 1.0f);
    MakeBrush(dc, theme.text_secondary, brText_);
    const auto footer = MakePreviewFooterLayout(toggle, s);
    const float cx = footer.chevron.x;
    const float cy = footer.chevron.y;
    const float direction = d.preview_only ? -1.0f : 1.0f;
    dc->DrawLine(D2D1::Point2F(cx - 4 * s, cy - direction * 2 * s),
        D2D1::Point2F(cx, cy + direction * 2 * s), brText_.get(), 1.4f * s);
    dc->DrawLine(D2D1::Point2F(cx, cy + direction * 2 * s),
        D2D1::Point2F(cx + 4 * s, cy - direction * 2 * s), brText_.get(), 1.4f * s);
    if (d.preview_only) {
        const std::wstring caption_alias = d.path.empty() ? std::wstring()
                                                          : app::AliasStore::Instance().Shown(d.path);
        DrawTextEndEllipsis(compositor_, compositor_->SmallFormat(),
            brText_.get(), caption_alias.empty() ? d.name : caption_alias, footer.caption.left, footer.caption.top,
            footer.caption.right - footer.caption.left, footer.caption.bottom - footer.caption.top);
    }
    if (d.preview_only) { dc->PopAxisAlignedClip(); return; }
    const bool card_header_in_well = d.pc_card && d.preview_enabled && previewH > 1.0f;
    y = (card_header_in_well
             ? previewTop + previewH + 12.0f * s
             : previewTop + DetailsPreviewBand(previewH, s)) - d.scroll_y * s;
    // The well and the fold bar are pinned above the scrolling facts. A system
    // card has no fold bar, so without this its scrolled sheet would paint over
    // the pinned header (and above the card's own top edge). A well without a
    // fold bar (icon only) still pins the well, not the panel top.
    const float foldBottom = hit.preview_toggle.bottom > hit.preview_toggle.top
        ? hit.preview_toggle.bottom
        : (d.preview_enabled && previewH > 0.0f ? previewTop + previewH : panel.top);
    const float restTop = card_header_in_well
        ? previewTop + previewH
        : std::max(panel.top, foldBottom);
    const D2D1_RECT_F restClip = D2D1::RectF(
        panel.left, restTop,
        panel.right, panel.bottom);
    dc->PushAxisAlignedClip(restClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // The reveal: an opacity mask whose soft edge sweeps down, and a rise.
    bool sweeping = false;
    D2D1_MATRIX_3X2_F sweep_saved{};
    if (dm.sweep_at) {
        const float t = static_cast<float>(motion_now_ - dm.sweep_at) / kDetailsSweepMs;
        if (t >= 1.0f) {
            dm.sweep_at = 0;
        } else {
            const float p = std::clamp(t, 0.0f, 1.0f);
            const float e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
            const float band = kDetailsSweepBandDip * s;
            const float edge = restClip.top - band + e * (restClip.bottom - restClip.top + 2.0f * band);
            const D2D1_GRADIENT_STOP stops[2] = {{0.0f, D2D1::ColorF(0, 0, 0, 1.0f)},
                                                 {1.0f, D2D1::ColorF(0, 0, 0, 0.0f)}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> mask;
            if (SUCCEEDED(dc->CreateGradientStopCollection(stops, 2, &collection)) &&
                SUCCEEDED(dc->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, edge - band), D2D1::Point2F(0.0f, edge)),
                    collection.get(), &mask))) {
                dc->PushLayer(D2D1::LayerParameters1(restClip, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                     D2D1::IdentityMatrix(), 1.0f, mask.get()),
                              nullptr);
                dc->GetTransform(&sweep_saved);
                dc->SetTransform(sweep_saved * D2D1::Matrix3x2F::Translation(0.0f, kDetailsRiseDip * s * (1.0f - e)));
                sweeping = true;
            }
            RequestMotionFrame();
        }
    }

    // Name + star + rename pencil + subtitle. The 此电脑 card already shows
    // its title and identity in the well; only a collapsed well needs this row.
    if (!card_header_in_well) {
        if (d.multi_count <= 1 && !d.context) {
            const bool starHot = IsHovered(vm, HitTestResult::DetailsStar);
            const bool renHot = IsHovered(vm, HitTestResult::DetailsRename);
            if (starHot || d.starred) {
                MakeBrush(dc, d.starred ? WithAlpha(theme.accent, starHot ? 0.28f : 0.18f)
                                        : theme.fill_hover, brFillHover_);
                FillRoundedRect(dc, brFillHover_.get(), hit.star.left, hit.star.top,
                                22.0f * s, 22.0f * s, 5.0f * s);
            }
            if (renHot) {
                MakeBrush(dc, theme.fill_hover, brFillHover_);
                FillRoundedRect(dc, brFillHover_.get(), hit.rename.left, hit.rename.top,
                                22.0f * s, 22.0f * s, 5.0f * s);
            }
            // Preview on/off: accent-tinted while on, plain while off. Lives on
            // the name row so it is reachable with the well collapsed.
            if (hit.preview_enable.right > hit.preview_enable.left) {
                const bool prevHot = IsHovered(vm, HitTestResult::DetailsPreviewEnable);
                if (prevHot || d.preview_enabled) {
                    MakeBrush(dc, d.preview_enabled
                        ? WithAlpha(theme.accent, prevHot ? 0.28f : 0.18f) : theme.fill_hover,
                        brFillHover_);
                    FillRoundedRect(dc, brFillHover_.get(), hit.preview_enable.left,
                                    hit.preview_enable.top, 22.0f * s, 22.0f * s, 5.0f * s);
                }
                DrawIconText(hit.preview_enable.left, hit.preview_enable.top, 22.0f * s,
                             22.0f * s, L"\xE890", L"eye",
                             d.preview_enabled ? theme.accent : theme.text_secondary, 0.72f);
            }
            DrawIconText(hit.star.left, hit.star.top, 22.0f * s, 22.0f * s,
                         d.starred ? L"\xE735" : L"\xE734", L"*",
                         d.starred ? theme.accent : theme.text_secondary, 1.0f);
            DrawIconText(hit.rename.left, hit.rename.top, 22.0f * s, 22.0f * s,
                         L"\xE8AC", L"ren", theme.text_secondary, 0.72f);
        }
        // 别名: the panel names the item the way the list does.
        const std::wstring alias = d.multi_count <= 1 && !d.path.empty() && !d.context
            ? app::AliasStore::Instance().Shown(d.path) : std::wstring();
        std::wstring shown = alias.empty() ? d.name : alias;
        if (d.multi_count > 1) {
            wchar_t selected[64]{};
            swprintf_s(selected,
                pulse::l10n::Get(pulse::l10n::StringId::SelectedCountFormat).c_str(),
                d.multi_count);
            shown = selected;
        }
        const float nameRight = d.multi_count <= 1 && !d.context
            ? hit.preview_enable.left - 4.0f * s : panel.right - pad;
        text(shown, D2D1::RectF(panel.left + pad, y, nameRight,
                                y + 22.0f * s),
             compositor_->HeaderFormat(), theme.text);
        std::wstring subtitle = d.subtitle_text;
        if (subtitle.empty())
            subtitle = d.type_text.empty() ? FormatListType(d.name, d.is_dir) : d.type_text;
        // 别名 + 原名: the real name leads the line under the alias.
        if (!alias.empty() && app::AliasStore::Instance().Display() == app::AliasDisplay::Both)
            subtitle = subtitle.empty() ? d.name : d.name + L" \u00B7 " + subtitle;
        if (!subtitle.empty())
            text(subtitle, D2D1::RectF(panel.left + pad, y + 22.0f * s,
                                       panel.right - pad, y + 38.0f * s),
                 compositor_->SmallFormat(), theme.text_secondary);
        y += 22.0f * s + 16.0f * s + 8.0f * s;
    }

    // Button row: 打开 / 在新标签打开 / 复制路径 / 更多 (icon over label).
    // A context card has no actions: nothing is selected to act on.
    if (d.multi_count <= 1 && !d.context) {
        auto rowButton = [&](const D2D1_RECT_F& rc, const wchar_t* glyph,
                             const wchar_t* fallback, const wchar_t* label, bool hovered,
                             bool primary, int64_t copied_ms = -1) {
            if (primary) {
                MakeBrush(dc, hovered ? theme.accent_hover : theme.accent, brAccent_);
                FillRoundedRect(dc, brAccent_.get(), rc.left, rc.top, rc.right - rc.left,
                                rc.bottom - rc.top, 6.0f * s);
            } else {
                if (hovered) {
                    MakeBrush(dc, theme.fill_hover, brFillHover_);
                    FillRoundedRect(dc, brFillHover_.get(), rc.left, rc.top,
                                    rc.right - rc.left, rc.bottom - rc.top, 6.0f * s);
                }
                MakeBrush(dc, theme.stroke_card, brStrokeCard_);
                dc->DrawRoundedRectangle(D2D1::RoundedRect(rc, 6.0f * s, 6.0f * s),
                                         brStrokeCard_.get(), 1.0f);
            }
            const D2D1_COLOR_F fg = primary ? theme.accent_text : theme.text;
            if (copied_ms >= 0) {
                // Copy confirmation: the icon pops into a check mark in place.
                const float pop = motion::SystemAnimationsEnabled()
                    ? motion::EaseOutBack(static_cast<float>(copied_ms) /
                                          static_cast<float>(motion::CopyFeedback::kPopMs))
                    : 1.0f;
                const float cx = (rc.left + rc.right) * 0.5f;
                const float cy = rc.top + 11.0f * s;
                const float half = 9.0f * s;
                D2D1_MATRIX_3X2_F saved{};
                dc->GetTransform(&saved);
                dc->SetTransform(D2D1::Matrix3x2F::Scale(std::max(0.05f, pop), std::max(0.05f, pop),
                                                         D2D1::Point2F(cx, cy)) * saved);
                (void)half;
                DrawIconText(rc.left, rc.top + 3.0f * s, rc.right - rc.left, 16.0f * s,
                             L"\xE73E", L"\x2713", theme.accent, 1.0f);
                dc->SetTransform(saved);
            } else {
                DrawIconText(rc.left, rc.top + 3.0f * s, rc.right - rc.left, 16.0f * s,
                             glyph, fallback, primary ? theme.accent_text
                                                      : theme.text_secondary, 1.0f);
            }
            IDWriteTextFormat* fmt = compositor_->SmallFormat();
            if (!fmt) return;
            // Draw through a private text layout so the shared format is never
            // mutated; narrow cells ellipsize (hover tooltip keeps full text).
            const float textW = rc.right - rc.left - 4.0f * s;
            const float textH = rc.bottom - rc.top - 22.0f * s;
            ComPtr<IDWriteTextLayout> tl;
            if (compositor_->DwriteFactory() &&
                SUCCEEDED(compositor_->DwriteFactory()->CreateTextLayout(
                    label, static_cast<UINT32>(wcslen(label)), fmt,
                    textW, textH, &tl)) && tl.get()) {
                tl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                const DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                tl->SetTrimming(&trim, nullptr);
                MakeBrush(dc, fg, brText_);
                compositor_->DrawTextLayout(D2D1::Point2F(rc.left + 2.0f * s, rc.top + 21.0f * s),
                                   tl.get(), brText_.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        };
        rowButton(hit.open, L"\xE8B7", L"O", pulse::l10n::Get(kDetailsButtonLabels[0]).c_str(),
                  IsHovered(vm, HitTestResult::DetailsOpen), true);
        rowButton(hit.new_tab, L"\xE8A7", L"\x2197", pulse::l10n::Get(kDetailsButtonLabels[1]).c_str(),
                  IsHovered(vm, HitTestResult::DetailsNewTab), false);
        const int64_t copied_ms = copy_feedback_.Elapsed(
            static_cast<int>(HitTestResult::DetailsCopyPath), 0, GetTickCount64());
        rowButton(hit.copy_path, L"\xE8C8", L"C",
                  copied_ms >= 0 ? pulse::l10n::Get(pulse::l10n::StringId::CopiedShort).c_str()
                                 : pulse::l10n::Get(kDetailsButtonLabels[2]).c_str(),
                  IsHovered(vm, HitTestResult::DetailsCopyPath), false, copied_ms);
        rowButton(hit.more, L"\xE712", L"...", pulse::l10n::Get(kDetailsButtonLabels[3]).c_str(),
                  IsHovered(vm, HitTestResult::DetailsMore), false);
        y += 40.0f * s + 4.0f * s;
    }

    auto infoRow = [&](const wchar_t* label, const std::wstring& value, float& iy) {
        text(label, D2D1::RectF(panel.left + pad, iy, panel.left + pad + 96.0f * s,
                                iy + 18.0f * s),
             compositor_->SmallFormat(), theme.text_secondary);
        text(value, D2D1::RectF(panel.left + pad + 100.0f * s, iy, panel.right - pad,
                                iy + 18.0f * s),
             compositor_->SmallFormat(), theme.text);
        iy += 18.0f * s;
    };

    if (d.pc_card) {
        // The system card: one fact per line, the key in the accent ink and the
        // value in a column shared by every row - a dense system sheet rather
        // than a label/value form. A value too long for its column continues on
        // a second line instead of being cut mid-name. A folder card instead
        // reuses the sections below, exactly as a selected folder does.
        IDWriteTextFormat* small_fmt = compositor_->SmallFormat();
        IDWriteFactory2* factory = compositor_->DwriteFactory();
        const float content_w = panel.right - panel.left - pad * 2.0f;
        const float key_w = DetailsFactKeyWidth(d, content_w, s, factory, small_fmt);
        const float valueX = panel.left + pad + key_w;
        const float value_w = content_w - key_w;
        const float avail = std::max(1.0f, value_w - 2.0f * s);
        for (const auto& row : d.context_rows) {
            text(row.first, D2D1::RectF(panel.left + pad, y, valueX - 4.0f * s,
                                        y + kDetailsFactLineDip * s),
                 small_fmt, theme.accent);
            MakeBrush(dc, theme.text, brText_);
            const auto split = SplitDetailLine(row.second, avail, factory, small_fmt);
            DrawTextEndEllipsis(compositor_, small_fmt, brText_.get(), split.first, valueX, y,
                                value_w, kDetailsFactLineDip * s);
            if (!split.second.empty())
                DrawTextEndEllipsis(compositor_, small_fmt, brText_.get(), split.second, valueX,
                                    y + kDetailsFactLineDip * s, value_w,
                                    kDetailsFactLineDip * s);
            y += DetailsFactRowHeight(row.second, value_w, s, factory, small_fmt);
        }
        y += 8.0f * s;
    } else if (d.multi_count > 1) {
        text(pulse::l10n::Get(pulse::l10n::StringId::Information),
             D2D1::RectF(panel.left + 12.0f * s, y, panel.right - 12.0f * s,
                                          y + 20.0f * s),
             compositor_->SmallFormat(), theme.accent);
        float iy = y + 20.0f * s;
        infoRow(pulse::l10n::Get(pulse::l10n::StringId::Location).c_str(), d.location_text, iy);
        infoRow(pulse::l10n::Get(pulse::l10n::StringId::KnownSize).c_str(), d.size_text, iy);
        y = iy + 8.0f * s;
    }

    if (d.multi_count <= 1 && !d.pc_card) {
        for (const auto& def : kDetailsSections) {
            if (d.context && !DetailsCardShowsSection(def.id)) continue;
            // Section separator (matches the 8 DIP gap in LayoutDetailsPanel).
            y += 8.0f * s;
            MakeBrush(dc, theme.stroke_card, brStrokeCard_);
            FillRect(dc, brStrokeCard_.get(), panel.left + pad, y - 4.5f * s,
                     panel.right - panel.left - pad * 2.0f, 1.0f);
            section(pulse::l10n::Get(def.label).c_str(), y, def.id);
            y += 28.0f * s;
            if ((d.collapsed_mask >> def.id) & 1u) { y += 6.0f * s; continue; }
            switch (def.id) {
            case 0: { // 基本信息
                float iy = y;
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::Location).c_str(), d.location_text, iy);
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::ColumnType).c_str(), !d.type_text.empty()
                            ? d.type_text : FormatListType(d.name, d.is_dir), iy);
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::ColumnSize).c_str(),
                        d.size_pending ? pulse::l10n::Get(pulse::l10n::StringId::Calculating)
                                                : d.size_text, iy);
                if (d.is_dir && d.size_manual) {
                    // 计算大小: text button like the 安全 section's 更改,
                    // left-aligned with the fact values.
                    const bool hot = IsHovered(vm, HitTestResult::DetailsSizeCalc);
                    text(pulse::l10n::Get(pulse::l10n::StringId::DetailsCalcSize),
                         D2D1::RectF(hit.size_calc.left, hit.size_calc.top,
                                     hit.size_calc.right, hit.size_calc.bottom),
                         compositor_->SmallFormat(),
                         hot ? theme.accent_hover : theme.accent);
                }
                if (DetailsShowsContains(d))
                    infoRow(pulse::l10n::Get(pulse::l10n::StringId::Contains).c_str(), d.contains_text, iy);
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::Created).c_str(), d.created_text, iy);
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::ModifiedTime).c_str(), d.modified_text, iy);
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::LastAccessed).c_str(), d.accessed_text, iy);
                for (const auto& property : d.preview_properties)
                    infoRow(pulse::l10n::ServiceText(property.label).c_str(), property.value, iy);
                y = iy + 8.0f * s;
                break;
            }
            case 1: { // 属性: writable checkboxes + 高级… on one row
                fluent::ControlState roState;
                roState.checked = (d.attrs & FILE_ATTRIBUTE_READONLY) != 0;
                roState.hovered = IsHovered(vm, HitTestResult::DetailsAttrToggle, 0);
                painter_.DrawCheckBox(hit.attr_readonly,
                    pulse::l10n::Get(pulse::l10n::StringId::ReadOnly), roState);
                fluent::ControlState hidState;
                hidState.checked = (d.attrs & FILE_ATTRIBUTE_HIDDEN) != 0;
                hidState.hovered = IsHovered(vm, HitTestResult::DetailsAttrToggle, 1);
                painter_.DrawCheckBox(hit.attr_hidden,
                    pulse::l10n::Get(pulse::l10n::StringId::Hidden), hidState);
                {
                    const bool hot = IsHovered(vm, HitTestResult::DetailsAttrToggle, 2);
                    const auto& rc = hit.attr_advanced;
                    if (hot) {
                        MakeBrush(dc, theme.fill_hover, brFillHover_);
                        FillRoundedRect(dc, brFillHover_.get(), rc.left, rc.top,
                                        rc.right - rc.left, rc.bottom - rc.top, 6.0f * s);
                    }
                    MakeBrush(dc, theme.stroke_card, brStrokeCard_);
                    dc->DrawRoundedRectangle(D2D1::RoundedRect(rc, 6.0f * s, 6.0f * s),
                                             brStrokeCard_.get(), 1.0f);
                    IDWriteTextFormat* fmt = compositor_->SmallFormat();
                    if (fmt) {
                        const auto old = fmt->GetTextAlignment();
                        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        MakeBrush(dc, theme.text, brText_);
                        DrawTextRect(dc, fmt, brText_.get(),
                                     pulse::l10n::Get(pulse::l10n::StringId::Advanced),
                                     rc.left, rc.top, rc.right - rc.left,
                                     rc.bottom - rc.top);
                        fmt->SetTextAlignment(old);
                    }
                }
                y += 24.0f * s + 8.0f * s;
                break;
            }
            case 2: { // 标签: 添加标签 row + preset chips (toggle on click)
                ghostButton(hit.tag_add, kIconAdd,
                            pulse::l10n::Get(pulse::l10n::StringId::AddTag),
                            IsHovered(vm, HitTestResult::DetailsTagAdd));
                for (size_t i = 0; i < hit.preset_chips.size(); ++i) {
                    const auto& rc = hit.preset_chips[i];
                    const auto& chip = d.preset_tags[i];
                    const bool hot = IsHovered(vm, HitTestResult::DetailsPresetTag,
                                               chip.tag_index);
                    D2D1_COLOR_F fill = chip.color;
                    fill.a *= chip.assigned ? (hot ? 0.34f : 0.24f)
                                            : (hot ? 0.10f : 0.05f);
                    MakeBrush(dc, fill, brFillHover_);
                    FillRoundedRect(dc, brFillHover_.get(), rc.left, rc.top,
                                    rc.right - rc.left, rc.bottom - rc.top, 11.0f * s);
                    D2D1_COLOR_F border = chip.color;
                    border.a *= chip.assigned ? 0.55f : 0.22f;
                    MakeBrush(dc, border, brStrokeCard_);
                    dc->DrawRoundedRectangle(D2D1::RoundedRect(rc, 11.0f * s, 11.0f * s),
                                             brStrokeCard_.get(), 1.0f);
                    MakeBrush(dc, chip.color, brTagDot_);
                    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rc.left + 11.0f * s,
                        (rc.top + rc.bottom) * 0.5f), 3.5f * s, 3.5f * s), brTagDot_.get());
                    const D2D1_COLOR_F saved_bg = text_background_;
                    text_background_ = BlendOver(fill, theme.bg);
                    text(chip.name, D2D1::RectF(rc.left + 19.0f * s, rc.top,
                                                rc.right - 4.0f * s, rc.bottom),
                         compositor_->SmallFormat(),
                         chip.assigned ? theme.text : theme.text_secondary);
                    text_background_ = saved_bg;
                }
                y = hit.preset_chips.empty() ? (y + 26.0f * s + 6.0f * s + 8.0f * s)
                    : (hit.preset_chips.back().bottom + 8.0f * s);
                break;
            }
            case 3: { // 安全
                float iy = y;
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::Owner).c_str(),
                        d.owner_text.empty() ? pulse::l10n::Get(pulse::l10n::StringId::LoadingEllipsis)
                                             : d.owner_text, iy);
                {
                    const bool hot = IsHovered(vm, HitTestResult::DetailsSecurityChange);
                    text(pulse::l10n::Get(pulse::l10n::StringId::Change),
                         D2D1::RectF(hit.security_change.left, y,
                                     hit.security_change.right, y + 18.0f * s),
                         compositor_->SmallFormat(),
                         hot ? theme.accent_hover : theme.accent, true);
                }
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::Permissions).c_str(),
                        d.permissions_text.empty() ? pulse::l10n::Get(pulse::l10n::StringId::LoadingEllipsis)
                                                   : d.permissions_text, iy);
                y = iy + 8.0f * s;
                break;
            }
            case 4: { // 其他
                float iy = y;
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::Drive).c_str(),
                        d.drive_text.empty() ? pulse::l10n::Get(pulse::l10n::StringId::LoadingEllipsis)
                                             : d.drive_text, iy);
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::FileSystem).c_str(),
                        d.fs_text.empty() ? pulse::l10n::Get(pulse::l10n::StringId::LoadingEllipsis)
                                          : d.fs_text, iy);
                infoRow(pulse::l10n::Get(pulse::l10n::StringId::FreeSpace).c_str(),
                        d.free_space_text.empty() ? pulse::l10n::Get(pulse::l10n::StringId::LoadingEllipsis)
                                                  : d.free_space_text, iy);
                y = iy + 8.0f * s;
                break;
            }
            default: break;
            }
        }
    }
    if (sweeping) {
        dc->SetTransform(sweep_saved);
        dc->PopLayer();
    }
    dc->PopAxisAlignedClip();
    dc->PopAxisAlignedClip();
}

float MainRenderer::DetailsContentHeightDip(const WindowViewModel& vm, float w, float h) {
    const D2D1_RECT_F panel = DetailsPanelRect(w, h);
    if (panel.right - panel.left <= 1.0f) return 0.0f;
    DetailsHitRects hit;
    LayoutDetailsPanel(panel, scale_, vm.details,
                       compositor_ ? compositor_->DwriteFactory() : nullptr,
                       compositor_ ? compositor_->SmallFormat() : nullptr,
                       compositor_, DetailsPanelPreviewHeight(vm.details, panel, scale_), hit);
    return hit.content_height_dip;
}

} // namespace pulse::ui