#pragma once
#include "vcui/vcui.h"
#include "core/IconStore.h"

namespace vcl {

// 圆角方形图标：从 IconStore 取位图，按 cover 方式铺满自己，取不到时画占位块。
// 放在卡片里当纯装饰，不参与命中测试。
class IconImageView : public vcui::Widget {
public:
    IconStore* store = nullptr;
    std::wstring url;
    float Radius = 2.f;
    D2D1_SIZE_F size = D2D1::SizeF(44.f, 44.f);

    void SetSource(IconStore* s, const std::wstring& u) { store = s; url = u; }

    D2D1_SIZE_F Desired(float, float) override {
        return D2D1::SizeF(W > 0 ? W : size.width, H > 0 ? H : size.height);
    }
    void Paint(vcui::PaintCtx& ctx) override;

private:
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> geo_;
    Microsoft::WRL::ComPtr<ID2D1Layer> layer_;
    float geoRad_ = -1.f;
    D2D1_RECT_F geoRect_{};
};

} // namespace vcl

