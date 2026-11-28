#include "ui/styles/token.h"

#include <algorithm>

namespace vcl::tok {

const std::vector<unsigned> Swatches{
    SwatchBlue, SwatchTeal, SwatchGreen, SwatchAmber, SwatchRed, SwatchPurple, SwatchMagenta};

namespace {

struct Base {
    unsigned kp, bj, xt, wz;
};

Base BaseColors(bool dark) {
    if (dark) return Base{0x2b2b2b, 0x202020, 0x2d2d2d, 0xf7f7f7};
    return Base{0xfbfbfb, 0xf3f3f3, 0xeaeaea, 0x1b1b1b};
}

} // namespace

vcui::Metrics Metrics() {
    vcui::Metrics m;
    m.radiusBtn = RadiusControl;
    m.radiusInput = RadiusControl;
    m.radiusCard = RadiusCard;
    m.radiusPanel = RadiusPanel;
    m.controlH = ControlH;
    m.rowH = RowH;
    m.gap = GapLg;
    m.padCard = Gap2Xl;
    m.borderW = StrokeHair;
    return m;
}

void Apply(vcui::Theme& th, bool highContrast) {
    th.m = Metrics();

    Base b = BaseColors(th.dark);
    vcui::Tokens& t = th.t;
    t.dark = th.dark;

    D2D1_COLOR_F accent = th.accent;
    if (highContrast) {
        b = th.dark ? Base{0x000000, 0x000000, 0x000000, 0xFFFFFF}
                    : Base{0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0x000000};
        accent = vcui::Hex(th.dark ? HcAccentDark : HcAccentLight);
        th.m.borderW = StrokeFocus;
    }

    t.kp = vcui::Hex(b.kp);
    t.bj = vcui::Hex(b.bj);
    t.xt = vcui::Hex(b.xt);
    t.wz = vcui::Hex(b.wz);

    t.accent = accent;
    t.onAccent = vcui::Luma(t.accent) > .58f ? vcui::Hex(OnAccentDark) : vcui::Hex(OnAccentLight);
    t.accentHover = vcui::Lighten(t.accent, .08f);
    t.accentPress = vcui::Darken(t.accent, .10f);
    t.accentSoft = vcui::A(t.accent, AccentSoftAlpha);
    t.accentSel = vcui::A(t.accent, AccentSelAlpha);

    t.text2 = highContrast ? t.wz : vcui::Mix(t.wz, t.bj, Text2Shift);
    t.text3 = highContrast ? vcui::Mix(t.wz, t.bj, .15f) : vcui::Mix(t.wz, t.bj, Text3Shift);
    t.border = highContrast ? t.wz : (th.dark ? vcui::Overlay(t, DarkBorderAlpha) : vcui::Mix(t.wz, t.kp, BorderShift));
    t.divider = highContrast ? vcui::Mix(t.wz, t.kp, .55f) : (th.dark ? vcui::Overlay(t, DarkDividerAlpha) : vcui::Mix(t.wz, t.kp, DividerShift));
    t.pressed = highContrast ? vcui::Mix(t.xt, t.wz, .30f) : vcui::Mix(t.xt, t.wz, th.dark ? .08f : PressedShift);
    t.panel = highContrast ? t.kp : vcui::Mix(t.kp, t.bj, PanelShift);
}

} // namespace vcl::tok
