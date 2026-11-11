// ui_details_panel_local.cpp — Fork-only details panel drawing (rules: local/merge-policy.md).
//
// The panel's local redesign, kept out of MainRenderer::DrawDetailsPanel so that
// upstream's function keeps one call per branch: the 此电脑 identity block, the
// icon-only well, the 下载并预览 well, the 此电脑 fact sheet (返回下一个 y) and the
// size row's 计算大小 button. Each block is moved verbatim (comments included);
// only the function boundary and three call sites changed:
//
//   - the well's 下载并预览 rect comes from details_cloud_button_, and the size
//     row's from details_size_calc_: snapshots of this frame's LayoutDetailsPanel
//     output, taken in DrawDetailsPanel right after the layout call and cleared
//     next to details_preview_bounds_ before its early return. The draw function
//     used to read the hit struct directly; the members keep the signatures short.
//   - the panel's `text` lambda lives in DrawDetailsPanel's scope and draws with
//     the member DrawTextRect, so the local blocks use DrawDetailsText below.
//   - the fact sheet recomputes DrawDetailsPanel's 12 dip content padding.
//
// ReleasePreviewHosts is here as well: it belongs to the 启用预览 master switch
// (app_commands.cpp), but it releases the hosts behind this well and the hover
// card, and its definition sat in the middle of the draw function.

#include "ui_renderer.h"
#include "ui_renderer_internal.h"
#include "fluent_components.h"
#include "../app/resource.h"
#include "../common/localization.h"
#include "../common/text_format.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace pulse::ui {

void MainRenderer::DrawDetailsText(ID2D1DeviceContext* dc, const std::wstring& str,
                                   const D2D1_RECT_F& rc, IDWriteTextFormat* fmt,
                                   const D2D1_COLOR_F& color, bool right) {
    // Same body as DrawDetailsPanel's `text` lambda; DrawTextRect is a member, so
    // local blocks that draw text after leaving that function need their own copy.
    MakeBrush(dc, color, brText_);
    if (!fmt) return;
    const auto old = fmt->GetTextAlignment();
    fmt->SetTextAlignment(right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    DrawTextRect(dc, fmt, brText_.get(), str, rc.left, rc.top,
                 rc.right - rc.left, rc.bottom - rc.top);
    fmt->SetTextAlignment(old);
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

void MainRenderer::DrawDetailsPcCardIdentity(ID2D1DeviceContext* dc, const Theme& theme,
                                             const DetailsPanelView& d,
                                             const D2D1_RECT_F& contentRc) {
    const float s = scale_;
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
    DrawDetailsText(dc, d.name, D2D1::RectF(textX, textY, textX + textW, textY + title_h),
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
}

void MainRenderer::DrawDetailsIconOnlyWell(ID2D1DeviceContext* dc, const Theme& theme,
                                           const DetailsPanelView& d,
                                           const D2D1_RECT_F& contentRc) {
    const float s = scale_;
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
}

void MainRenderer::DrawDetailsCloudWell(const WindowViewModel& vm, ID2D1DeviceContext* dc,
                                        const Theme& theme, const DetailsPanelView& d,
                                        const D2D1_RECT_F& contentRc) {
    const float s = scale_;
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
    const bool button = !downloading &&
        details_cloud_button_.right > details_cloud_button_.left;
    const float bottom = button ? details_cloud_button_.top - 12.0f * s : contentRc.bottom;
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
        const D2D1_RECT_F& rc = details_cloud_button_;
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
}

float MainRenderer::DrawDetailsFactRows(ID2D1DeviceContext* dc, const Theme& theme,
                                        const DetailsPanelView& d, const D2D1_RECT_F& panel, float pad,
                                        float y) {
    const float s = scale_;
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
        DrawDetailsText(dc, row.first, D2D1::RectF(panel.left + pad, y, valueX - 4.0f * s,
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
    return y;
}

void MainRenderer::DrawDetailsSizeCalcText(const WindowViewModel& vm, ID2D1DeviceContext* dc,
                                           const Theme& theme) {
    // 计算大小: text button like the 安全 section's 更改,
    // left-aligned with the fact values.
    const bool hot = IsHovered(vm, HitTestResult::DetailsSizeCalc);
    DrawDetailsText(dc, pulse::l10n::Get(pulse::l10n::StringId::DetailsCalcSize),
                    D2D1::RectF(details_size_calc_.left, details_size_calc_.top,
                                details_size_calc_.right, details_size_calc_.bottom),
                    compositor_->SmallFormat(),
                    hot ? theme.accent_hover : theme.accent);
}

}  // namespace pulse::ui
