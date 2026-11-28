#pragma once

#include <vcui/Color.h>
#include <vcui/Theme.h>

#include <vector>

namespace vcl::tok {

// ---- 颜色 ----
inline constexpr unsigned Accent        = 0x0071EA;
inline constexpr const wchar_t* AccentHex = L"#0071EA";
inline constexpr unsigned Text2         = 0x606060;
inline constexpr unsigned Text2Soft     = 0x808080;
inline constexpr unsigned Text3         = 0x909090;
inline constexpr unsigned Text3Soft     = 0x999999;
inline constexpr unsigned Text3Muted    = 0x707070;
inline constexpr unsigned HcAccentDark  = 0x00FFFF;
inline constexpr unsigned HcAccentLight = 0x0000CC;
inline constexpr unsigned OnAccentDark  = 0x0b0b0c;
inline constexpr unsigned OnAccentLight = 0xffffff;
inline constexpr unsigned Danger        = 0xD13438;
inline constexpr unsigned Success       = 0x107C10;
inline constexpr unsigned Warning       = 0xC77A00;

inline constexpr unsigned SwatchBlue    = 0x0071EA;
inline constexpr unsigned SwatchTeal    = 0x00B7C3;
inline constexpr unsigned SwatchGreen   = 0x107C10;
inline constexpr unsigned SwatchAmber   = 0xD97706;
inline constexpr unsigned SwatchRed     = 0xD13438;
inline constexpr unsigned SwatchPurple  = 0x8764B8;
inline constexpr unsigned SwatchMagenta = 0xE3008C;

// ---- 圆角（DIP） ----
inline constexpr float RadiusNone    = 0.f;
inline constexpr float RadiusHair    = 8.f;
inline constexpr float RadiusControl = 8.f;
inline constexpr float RadiusCard    = 16.f;
inline constexpr float RadiusPanel   = 24.f;
inline constexpr float RadiusPill    = 999.f;

// ---- 线宽（DIP） ----
inline constexpr float StrokeHair  = 1.f;
inline constexpr float StrokeFocus = 2.f;

// ---- 间距（DIP） ----
inline constexpr float GapXs   = 4.f;
inline constexpr float GapSm   = 8.f;
inline constexpr float GapMd   = 10.f;
inline constexpr float GapLg   = 12.f;
inline constexpr float GapXl   = 16.f;
inline constexpr float Gap2Xl  = 24.f;
inline constexpr float PadPage = 16.f;
inline constexpr float PadCard = 20.f;
inline constexpr float PadRow  = 12.f;

// ---- 控件度量 ----
inline constexpr float ControlH = 34.f;
inline constexpr float RowH     = 44.f;

// ---- 主题底色的混合比例 ----
inline constexpr float Text2Shift  = .45f;
inline constexpr float Text3Shift  = .65f;
inline constexpr float BorderShift = .84f;
inline constexpr float DividerShift = .90f;
inline constexpr float PanelShift  = .14f;
inline constexpr float PressedShift = .07f;
inline constexpr float DarkBorderAlpha  = .14f;
inline constexpr float DarkDividerAlpha = .08f;
inline constexpr float AccentSoftAlpha  = .12f;
inline constexpr float AccentSelAlpha   = .18f;

extern const std::vector<unsigned> Swatches;

vcui::Metrics Metrics();
void Apply(vcui::Theme& th, bool highContrast = false);

} // namespace vcl::tok
