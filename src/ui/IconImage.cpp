#include "ui/IconImage.h"

namespace vcl {

void IconImageView::Paint(vcui::PaintCtx& ctx) {
    const vcui::Gfx& g = ctx.g;
    if (!g.rt) return;

    ID2D1Bitmap* bmp = store ? store->Acquire(url) : nullptr;
    if (!bmp) {
        // 占位：浅灰块 + 细边，保证异步加载期间卡片高度不跳
        ctx.g.RoundRect(fr_, Radius, vcui::Mix(ctx.t.kp, ctx.t.border, .55f));
        ctx.g.StrokeRect(fr_, Radius, ctx.t.border, ctx.Hair());
        return;
    }

    ID2D1RenderTarget* rt = g.rt.Get();
    const float rad = vcui::EffectiveCornerRadius(fr_, Radius);
    if (!layer_ || geoRad_ != rad || geoRect_.left != fr_.left || geoRect_.top != fr_.top ||
        geoRect_.right != fr_.right || geoRect_.bottom != fr_.bottom) {
        geo_.Reset();
        layer_.Reset();
        auto rr = D2D1::RoundedRect(fr_, rad, rad);
        if (SUCCEEDED(g.factory->CreateRoundedRectangleGeometry(rr, &geo_)) && geo_) {
            rt->CreateLayer(&layer_);
        }
        geoRad_ = rad;
        geoRect_ = fr_;
    }

    // 只有武器（几何遮罩）齐全时才走圆角裁剪，否则退回方角，宁可难看也不崩
    bool masked = geo_ && layer_;
    D2D1_LAYER_PARAMETERS lp = D2D1::LayerParameters();
    lp.geometricMask = geo_.Get();
    lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    lp.contentBounds = fr_;
    if (masked) rt->PushLayer(lp, layer_.Get());

    const D2D1_SIZE_F bsz = bmp->GetSize();
    if (bsz.width > 0.f && bsz.height > 0.f) {
        // cover：等比放大到铺满，居中裁掉超出的那一边
        const float dw = fr_.right - fr_.left, dh = fr_.bottom - fr_.top;
        const float k = (std::max)(dw / bsz.width, dh / bsz.height);
        const float sw = dw / k, sh = dh / k;
        const float sx = (bsz.width - sw) * .5f, sy = (bsz.height - sh) * .5f;
        D2D1_RECT_F dst = fr_;
        rt->DrawBitmap(bmp, dst, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                       D2D1::RectF(sx, sy, sx + sw, sy + sh));
    }

    if (masked) rt->PopLayer();
}

} // namespace vcl

