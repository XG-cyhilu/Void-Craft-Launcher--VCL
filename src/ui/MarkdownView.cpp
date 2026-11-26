#include "ui/MarkdownView.h"

#include "vcui/vcui.h"

#include <algorithm>

namespace vcl {
namespace {

// 按实际可用宽度折行并回报真实高度的 Label。
// 库里的 Label 只在 W > 0 时才算折行高度，而这里宽度是父容器给的，所以自己实现 Desired。
class WrapLabel : public vcui::Label {
public:
    D2D1_SIZE_F Desired(float availW, float availH) override {
        (void)availH;
        float w = W > 0 ? W : (availW > 0.f ? availW : 600.f);
        float h = H > 0 ? H : vcui::LineH(Size);
        if (GFX) {
            if (auto lay = GFX->Layout(Text, Size, Weight, L"Microsoft YaHei", Align, VAlignMode, w, 4096.f, true)) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(lay->GetMetrics(&tm)) && tm.height > 1.f) h = tm.height;
            }
        }
        return D2D1::SizeF(w, h);
    }
};

std::shared_ptr<WrapLabel> Para(const std::wstring& text, float size, int weight,
                                D2D1_COLOR_F color, bool wrap = true) {
    auto l = std::make_shared<WrapLabel>();
    l->Text = text;
    l->Size = size;
    l->Weight = weight;
    l->Color = color;
    l->Wrap = wrap;
    return l;
}

} // namespace

std::shared_ptr<vcui::Widget> BuildMarkdown(const std::vector<MdBlock>& blocks, const vcui::Tokens& t) {
    auto root = vcui::VBox(8.f);
    root->Grow = 1.f;

    for (const auto& b : blocks) {
        float indent = (float)b.indent * 16.f;

        switch (b.kind) {
        case MdKind::Heading: {
            float size = b.level <= 1 ? 19.f : (b.level == 2 ? 16.5f : 14.5f);
            auto l = Para(b.text, size, 600, t.wz);
            l->MT = 8.f;
            l->ML = indent;
            root->Add(l);
            break;
        }
        case MdKind::Paragraph: {
            auto l = Para(b.text, 13.f, 400, t.wz);
            l->ML = indent;
            root->Add(l);
            break;
        }
        case MdKind::Quote: {
            auto row = vcui::HBox(10.f);
            row->ML = indent;
            auto bar = std::make_shared<vcui::Widget>();
            bar->W = 3.f;
            bar->Bg = t.text3;
            bar->Bg.a = .5f;
            row->Add(bar);
            auto l = Para(b.text, 13.f, 400, t.text2);
            l->Grow = 1.f;
            row->Add(l);
            root->Add(row);
            break;
        }
        case MdKind::Bullet:
        case MdKind::Ordered: {
            auto row = vcui::HBox(7.f);
            row->ML = indent + 2.f;
            std::wstring mark = b.kind == MdKind::Bullet ? L"•" : (std::to_wstring(b.level) + L".");
            auto m = std::make_shared<vcui::Label>(mark, 13.f, 400);
            m->Color = t.accent;
            m->W = b.kind == MdKind::Bullet ? 12.f : 20.f;
            row->Add(m);
            auto l = Para(b.text, 13.f, 400, t.wz);
            l->Grow = 1.f;
            row->Add(l);
            root->Add(row);
            break;
        }
        case MdKind::Code: {
            auto card = std::make_shared<vcui::Card>();
            card->Shadow = false;
            card->Border = true;
            card->Radius = 8.f;
            card->Fill = t.xt;
            card->Pad(10.f);
            card->ML = indent;
            auto l = Para(b.text, 12.f, 400, t.wz);
            card->Add(l);
            root->Add(card);
            break;
        }
        case MdKind::Rule:
            root->Add(std::make_shared<vcui::Divider>());
            break;
        default:
            break;
        }
    }
    return root;
}

} // namespace vcl
