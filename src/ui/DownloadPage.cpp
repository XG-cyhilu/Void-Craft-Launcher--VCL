#include "ui/DownloadPage.h"
#include "ui/IconImage.h"
#include "ui/MarkdownView.h"
#include "core/Markdown.h"
#include "core/Paths.h"
#include "core/ModWiki.h"
#include "core/Text.h"

#include <fstream>
#include <cstdlib>
#include <algorithm>

#include <windows.h>
#include <shellapi.h>

namespace vcl {

struct DownloadPage::State {
    std::shared_ptr<vcui::Stack> list;
    std::shared_ptr<vcui::ScrollView> listScroll;
    std::shared_ptr<vcui::Stack> body;      // 列表 ↔ 详情
    std::shared_ptr<vcui::Stack> detail;    // 详情页（打开时重建内容）
    size_t detailGen = 0;                   // 丢弃过期的详情回调
    std::shared_ptr<vcui::Label> status;
    std::shared_ptr<vcui::Input> query;
    std::shared_ptr<vcui::Dropdown> mirror;
    std::shared_ptr<vcui::Dropdown> order;
    std::shared_ptr<vcui::Label> pageInfo;
    std::shared_ptr<vcui::Widget> toolbar;   // 详情页时收起
    std::shared_ptr<vcui::Widget> foot;      // 详情页时收起（分页对详情没意义）
    std::shared_ptr<vcui::Button> prev;
    std::shared_ptr<vcui::Button> next;

    // 游戏页：正式 / 快照 / 远古 三个频道，交给 VirtualList（自带滚动条，不分页）
    std::shared_ptr<vcui::VirtualList> gameList;
    std::shared_ptr<vcui::Segmented> channel;
    std::shared_ptr<std::vector<McVersion>> shown;
    int chan = 0;          // 0 正式 / 1 快照 / 2 远古

    std::vector<McVersion> versions;

    int sub = 0;
    std::wstring type;
    std::wstring index = L"relevance";
    std::wstring filter;

    int page = 0;          // 0-based
    int pages = 1;
    long long total = 0;
    size_t gen = 0;        // 丢弃过期回调
    bool bmclOk = true;    // 本机 BMCLAPI 是否可达
};

std::vector<std::wstring> DownloadPage::SubNav() {
    return { L"游戏", L"模组", L"整合包", L"资源包", L"光影包", L"数据包" };
}

DownloadPage::DownloadPage(UiCtx* ctx) : ctx_(ctx) {}

namespace {

int PageSizeFor(int sub) { return sub == 0 ? 30 : 20; }

// 每个条目一张小卡片：不投影、细描边，板式靠列表的 gap 撑开
std::shared_ptr<vcui::Card> ItemCard() {
    auto c = std::make_shared<vcui::Card>();
    c->Shadow = false;
    c->Border = true;
    c->Radius = 6.f;
    c->Pad(12.f);
    return c;
}

// 可点击的条目卡：整张卡是命中目标（点一下进详情），鼠标给手型
class ClickCard : public vcui::Card {
public:
    vcui::CursorShape CursorAt(float, float) const override { return vcui::CursorShape::Hand; }
};

std::shared_ptr<vcui::Card> ItemCardClick() {
    auto c = std::make_shared<ClickCard>();
    c->Shadow = false;
    c->Border = true;
    c->Radius = 6.f;
    c->Pad(12.f);
    c->Interactive = true;
    return c;
}

std::shared_ptr<vcui::Stack> ItemRow(float gap = 10.f) {
    auto r = vcui::HBox(gap);
    r->Cross = vcui::Stack::Center;
    return r;
}

// 游戏页的行：虚拟列表复用，字段留成可改写的
class GameRow : public vcui::Card {
public:
    std::shared_ptr<IconImageView> icon;
    std::shared_ptr<vcui::Badge> badge;
    std::shared_ptr<vcui::Label> name, date;
    std::shared_ptr<vcui::Button> btn;
};

// 详情页「版本」页签的行：虚拟列表复用行控件，所以字段留成可改写的
class VersionRow : public vcui::Card {
public:
    std::shared_ptr<vcui::Label> name, meta, size;
    std::shared_ptr<vcui::Button> btn;
};

// 列表是异步回来才填的，构建期的 StaggerIn 覆盖不到，所以每次填完自己补一遍错峰入场
void RevealRows(const std::shared_ptr<vcui::Stack>& list, const Settings* st) {
    if (!list || (st && !st->animations)) return;
    float d = 0.f;
    for (auto& k : list->Children()) {
        k->FadeIn(.20f, 8.f, d);
        d = (std::min)(d + .025f, .16f);
    }
}

// 版本类型 → resource\image 里的方块图标（打包在 exe 里，走 res: 源）
std::wstring VersionIconPath(const std::wstring& type) {
    std::wstring name = L"Grass_Block.png";                  // release：草方块
    if (type == L"snapshot") name = L"Command_Block.png";     // snapshot：命令方块
    else if (type == L"old_beta" || type == L"old_alpha") name = L"StoneOldBeta.png";
    return L"res:image/" + name;
}

// 汉化名优先；没有就退回 Modrinth 的英文标题
std::wstring DisplayTitle(const UiCtx* ctx, const MrHit& h) {
    if (ctx && ctx->wiki) {
        if (const std::wstring* zh = ctx->wiki->Zh(h.slug)) {
            if (!zh->empty()) return *zh;
        }
    }
    return h.title.empty() ? h.slug : h.title;
}

// Modrinth 网页地址（类型决定路径段）
std::wstring ModrinthPageUrl(const MrHit& h) {
    std::wstring kind = L"mod";
    if (h.projectType == L"modpack") kind = L"modpack";
    else if (h.projectType == L"resourcepack") kind = L"resourcepack";
    else if (h.projectType == L"shader") kind = L"shader";
    else if (h.projectType == L"datapack") kind = L"datapack";
    const std::wstring id = h.slug.empty() ? h.id : h.slug;
    return L"https://modrinth.com/" + kind + L"/" + id;
}

std::wstring McmodPageUrl(int classId) {
    return L"https://www.mcmod.cn/class/" + std::to_wstring(classId) + L"/";
}

void OpenExternal(const std::wstring& url) {
    if (url.empty()) return;
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace

std::shared_ptr<vcui::Widget> DownloadPage::Build(int sub) {
    auto page = vcui::VBox(10.f);
    page->Grow = 1.f;

    auto s = std::make_shared<State>();
    s->sub = sub;

    // 状态行/分页必须先建好：BuildGame / BuildModrinth 里的加载回调会立刻写状态
    s->status = Caption(L"");
    s->status->Grow = 1.f;
    s->prev = std::make_shared<vcui::Button>(L"上一页");
    s->prev->W = 88.f;
    s->pageInfo = std::make_shared<vcui::Label>(L"");
    s->pageInfo->UseType(vcui::Type::Caption1);
    s->pageInfo->W = 150.f;
    s->pageInfo->Align = vcui::HAlign::Center;
    s->next = std::make_shared<vcui::Button>(L"下一页");
    s->next->W = 88.f;

    if (sub == 0) {
        BuildGame(s, *page);
    } else {
        static const wchar_t* types[] = { L"", L"mod", L"modpack", L"resourcepack", L"shader", L"datapack" };
        s->type = types[sub < 6 ? sub : 1];
        BuildModrinth(s, *page);
    }

    auto self = this;
    // 游戏页改成了虚拟列表（自带滚动条、不分页），状态文案已经落在工具行里
    if (sub != 0) {
        auto foot = vcui::HBox(8.f);
        foot->Add(s->status);
        foot->Add(s->prev);
        foot->Add(s->pageInfo);
        foot->Add(s->next);
        page->Add(foot);
        s->foot = foot;
        s->prev->Clicked = [self, s] { self->GoPage(s, -1); };
        s->next->Clicked = [self, s] { self->GoPage(s, +1); };
    }
    return page;
}

// ---------------- 游戏（BMCLAPI / Mojang） ----------------

void DownloadPage::BuildGame(const std::shared_ptr<State>& s, vcui::Stack& page) {
    auto bar = vcui::HBox(8.f);
    s->mirror = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"BMCLAPI 镜像（推荐）", L"Mojang 官方" },
        ctx_->settings->mirror == L"mojang" ? 1 : 0);
    s->mirror->W = 196.f;
    s->mirror->Tooltip = L"版本清单与 jar 的下载源";

    s->query = std::make_shared<vcui::Input>(L"过滤版本号，例如 1.20.1");
    s->query->W = 260.f;

    auto refresh = std::make_shared<vcui::Button>(L"刷新");
    auto tip = Caption(L"源：BMCLAPI / Mojang");
    auto sp = vcui::Spacer(0.f);
    sp->Grow = 1.f;

    bar->Add(s->mirror);
    bar->Add(s->query);
    bar->Add(refresh);
    bar->Add(sp);
    bar->Add(tip);
    page.Add(bar);
    s->toolbar = bar;

    // 频道：正式 / 快照 / 远古。切换只重排列表，不重新请求
    auto chanRow = vcui::HBox(10.f);
    chanRow->Cross = vcui::Stack::Center;
    s->channel = std::make_shared<vcui::Segmented>(
        std::vector<std::wstring>{ L"正式", L"快照", L"远古" }, 0);
    auto chanWrap = vcui::HBox(0.f);
    chanWrap->Cross = vcui::Stack::Start;
    chanWrap->Add(s->channel);
    chanRow->Add(chanWrap);
    chanRow->Add(s->status);
    page.Add(chanRow);

    // 虚拟列表：只建视口内那几行，滚动条由列表自己画（ShowBar）
    s->gameList = std::make_shared<vcui::VirtualList>();
    s->gameList->RowHeight = 52.f;
    s->gameList->Gap = 6.f;
    s->gameList->ShowBar = true;
    s->gameList->BarW = 8.f;
    s->gameList->Grow = 1.f;
    page.Add(s->gameList);

    auto self = this;
    s->mirror->Changed = [self, s](int idx) {
        self->ctx_->settings->mirror = idx == 0 ? L"bmclapi" : L"mojang";
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        s->page = 0;
        self->LoadManifest(s);
    };
    refresh->Clicked = [self, s] { s->page = 0; self->LoadManifest(s); };
    s->query->Changed = [self, s](const std::wstring& q) {
        s->filter = q;
        self->RenderVersions(s);
    };
    s->channel->Changed = [self, s](int idx) {
        s->chan = idx;
        self->RenderVersions(s);
    };

    LoadManifest(s);
}

void DownloadPage::RenderVersions(const std::shared_ptr<State>& s) {
    std::wstring key = ToLower(Trim(s->filter));

    auto pass = [](const McVersion& v, int chan) {
        if (chan == 0) return v.type == L"release";
        if (chan == 1) return v.type == L"snapshot";
        return v.type == L"old_beta" || v.type == L"old_alpha";
    };

    auto shown = std::make_shared<std::vector<McVersion>>();
    shown->reserve(s->versions.size());
    for (const auto& v : s->versions) {
        if (!pass(v, s->chan)) continue;
        if (!key.empty() && ToLower(v.id).find(key) == std::wstring::npos) continue;
        shown->push_back(v);
    }
    s->shown = shown;
    s->total = (long long)shown->size();
    s->page = 0;
    s->pages = 1;

    auto self = this;
    auto list = s->gameList;
    list->Reset();
    list->Build = [self, s](int) {
        auto r = std::make_shared<GameRow>();
        r->Shadow = false;
        r->Border = true;
        r->Radius = 6.f;
        r->Pad(10.f);

        auto row = vcui::HBox(10.f);
        row->Cross = vcui::Stack::Center;
        r->icon = std::make_shared<IconImageView>();
        r->icon->size = D2D1::SizeF(32.f, 32.f);
        r->icon->Radius = 5.f;
        row->Add(r->icon);

        r->badge = std::make_shared<vcui::Badge>(L"", vcui::Badge::Neutral);
        r->badge->W = 80.f;
        row->Add(r->badge);

        r->name = Text(L"", vcui::Type::Body1Strong);
        r->name->Grow = 1.f;
        r->name->Ellipsis = true;
        row->Add(r->name);

        r->date = Caption(L"");
        r->date->W = 86.f;
        row->Add(r->date);

        r->btn = std::make_shared<vcui::Button>(L"安装");
        r->btn->W = 76.f;
        row->Add(r->btn);

        r->Add(row);
        return r;
    };
    list->Bind = [self, s, shown](vcui::Widget& w, int i) {
        auto* r = dynamic_cast<GameRow*>(&w);
        if (!r || i < 0 || i >= (int)shown->size()) return false;
        const McVersion v = (*shown)[(size_t)i];
        r->icon->SetSource(self->ctx_->icons, VersionIconPath(v.type));
        r->badge->Text = v.type;
        r->badge->ToneKind = v.type == L"release" ? vcui::Badge::Accent : vcui::Badge::Neutral;
        r->name->Text = v.id;
        r->date->Text = IsoDateOnly(v.releaseTime);
        r->btn->Clicked = [self, s, v] { self->InstallGame(s, v); };
        return true;
    };
    list->ItemCount = (int)shown->size();
    if (ctx_->settings->animations) list->AnimateIn();

    if (s->status)
        s->status->Text = WFormat(L"%s%lld 个版本", key.empty() ? L"" : L"筛选结果 ", s->total);
}

void DownloadPage::LoadManifest(const std::shared_ptr<State>& s) {
    s->status->Text = L"正在获取版本清单…";
    bool bmcl = s->mirror ? s->mirror->Selected == 0 : true;
    size_t gen = ++s->gen;
    auto self = this;
    // 镜像只留 8s 探测窗口：网络被拦截时能尽快回退官方
    ctx_->http->Get(McManifestUrl(bmcl), [self, s, bmcl, gen](bool ok, const std::string& body, const std::string& err) {
        if (s->gen != gen) return;
        if (!ok) {
            if (bmcl) {
                s->bmclOk = false;
                if (s->mirror) s->mirror->Selected = 1;
                s->status->Text = L"BMCLAPI 不可达，已自动回退 Mojang 官方源…";
                self->LoadManifest(s);
                return;
            }
            s->status->Text = L"版本清单获取失败：" + Utf8ToWide(err);
            return;
        }
        s->bmclOk = bmcl;
        std::string pe;
        if (!ParseMcManifest(body, s->versions, &pe)) {
            s->status->Text = L"清单解析失败：" + Utf8ToWide(pe);
            return;
        }
        if (bmcl)
            for (auto& v : s->versions) v.url = MirrorUrl(v.url, true);
        self->RenderVersions(s);
        if (s->status)
            s->status->Text = WFormat(L"%lld 个版本（%s）", s->total, bmcl ? L"BMCLAPI" : L"Mojang");
    }, {}, bmcl ? 8000 : 25000);
}

void DownloadPage::InstallGame(const std::shared_ptr<State>& s, const McVersion& v) {
    s->status->Text = L"正在读取 " + v.id + L" 的版本元数据…";
    auto ver = v;
    auto self = this;
    ctx_->http->Get(v.url, [self, s, ver](bool ok, const std::string& body, const std::string& err) {
        if (!ok) {
            s->status->Text = L"版本元数据获取失败：" + Utf8ToWide(err);
            return;
        }
        McClientFile f;
        std::string pe;
        if (!ParseMcVersionJson(body, f, &pe)) {
            s->status->Text = L"版本元数据不完整：" + Utf8ToWide(pe);
            return;
        }

        std::wstring root = CurrentRootPath(*self->ctx_->settings);
        std::wstring dir = JoinPath(JoinPath(root, L"versions"), ver.id);
        EnsureDir(dir);

        std::wstring jsonPath = JoinPath(dir, ver.id + L".json");
        {
            std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
            if (out) out.write(body.data(), (std::streamsize)body.size());
        }

        std::wstring jarPath = JoinPath(dir, ver.id + L".jar");
        std::wstring official = f.url;
        std::wstring mirrored = MirrorUrl(f.url, true);
        std::wstring primary = s->bmclOk ? mirrored : official;
        std::wstring fallback = s->bmclOk ? official : mirrored;
        std::vector<std::wstring> mirrors;
        if (fallback != primary) mirrors.push_back(fallback);

        if (self->ctx_->dl->Enqueue(primary, jarPath, f.sha1, f.size, mirrors)) {
            s->status->Text = WFormat(L"已加入队列：%s · client.jar %s", ver.id.c_str(),
                                      HumanBytes(f.size).c_str());
            self->ctx_->Toast(L"开始下载 " + ver.id, vcui::InfoTone::Success);
        } else {
            s->status->Text = L"入队失败（目标文件可能正被占用）";
        }
    });
}

// ---------------- Modrinth ----------------

void DownloadPage::BuildModrinth(const std::shared_ptr<State>& s, vcui::Stack& page) {
    auto bar = vcui::HBox(8.f);
    s->query = std::make_shared<vcui::Input>(L"搜索" + MrTypeName(s->type));
    s->query->W = 360.f;

    s->order = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"相关度", L"下载量", L"关注数" }, 0);
    s->order->W = 128.f;

    auto go = std::make_shared<vcui::Button>(L"搜索", true);
    auto tip = Caption(L"源：Modrinth");
    auto sp = vcui::Spacer(0.f);
    sp->Grow = 1.f;

    bar->Add(s->query);
    bar->Add(s->order);
    bar->Add(go);
    bar->Add(sp);
    bar->Add(tip);
    page.Add(bar);
    s->toolbar = bar;

    s->list = vcui::VBox(6.f);
    s->listScroll = std::make_shared<vcui::ScrollView>();
    s->listScroll->Grow = 1.f;
    s->listScroll->ShowBar = true;
    s->listScroll->Add(s->list);

    // 列表和详情共用同一块内容区：点卡片进详情，返回时列表原样还在
    s->body = vcui::VBox(0.f);
    s->body->Grow = 1.f;
    s->body->Add(s->listScroll);
    page.Add(s->body);

    auto self = this;
    s->order->Changed = [self, s](int idx) {
        s->index = idx == 0 ? L"relevance" : (idx == 1 ? L"downloads" : L"follows");
        self->LoadSearch(s, 0);
    };
    go->Clicked = [self, s] { self->LoadSearch(s, 0); };
    s->query->Submitted = [self, s] { self->LoadSearch(s, 0); };

    // 自检钩子：VCL_UI_TEST=mods + VCL_UI_QUERY=更多箱子 直接落到"中文检索"的结果上
    if (const wchar_t* q = _wgetenv(L"VCL_UI_QUERY")) {
        if (*q) s->query->Text = q;
    }
    LoadSearch(s, 0);
}

void DownloadPage::LoadSearch(const std::shared_ptr<State>& s, int pageIdx) {
    int size = PageSizeFor(s->sub);
    s->status->Text = L"正在查询 Modrinth…";
    size_t gen = ++s->gen;
    auto self = this;

    // 中文查询：先在汉化表里命中 slug，再拿条目自带的英文名去查 Modrinth，
    // 返回后用命中的 slug 集合过滤（表里没有中文名时才退回英文名结果）
    std::wstring typed = Trim(s->query ? s->query->Text : L"");
    std::wstring remoteQ = typed;
    std::vector<std::wstring> zhSlugs;
    bool zhSearch = ModWiki::HasCjk(typed) && ctx_->wiki && ctx_->wiki->Loaded();
    if (zhSearch) {
        std::wstring en;
        ctx_->wiki->ResolveZh(typed, zhSlugs, en);
        if (!en.empty()) remoteQ = en;
        else if (!zhSlugs.empty()) remoteQ = zhSlugs.front();   // 没括号就用 slug 试一把
        if (zhSlugs.empty()) {
            s->status->Text = L"汉化表里没有这个词，试试英文名或缩写";
            s->list->Clear();
            s->total = 0;
            s->pages = 1;
            s->page = 0;
            UpdatePager(s);
            return;
        }
    }

    // 中文检索是一次抓齐再本地筛选：一次多要点，让汉化命中的项目尽量落在同一页
    int fetchSize = zhSearch ? (std::min)(100, (int)zhSlugs.size() * 3 + 10) : size;

    ctx_->http->Get(MrSearchUrl(s->type, remoteQ, s->index, fetchSize, zhSearch ? 0 : pageIdx * size),
                    [self, s, gen, pageIdx, size, zhSearch, zhs = std::move(zhSlugs)](bool ok, const std::string& body,
                                                                                    const std::string& err) {
                        if (s->gen != gen) return;
                        if (!ok) {
                            s->status->Text = L"Modrinth 查询失败：" + Utf8ToWide(err);
                            return;
                        }
                        std::vector<MrHit> hits;
                        long long total = 0;
                        std::string pe;
                        if (!ParseMrSearch(body, hits, &pe, &total)) {
                            s->status->Text = L"响应解析失败：" + Utf8ToWide(pe);
                            return;
                        }
                        if (!zhs.empty()) {
                            // 中文检索：只留下汉化表里命中的项目；一个都没命中就当英文检索的结果
                            std::vector<MrHit> keep;
                            for (const auto& h : hits)
                                if (std::find(zhs.begin(), zhs.end(), ToLower(h.slug)) != zhs.end())
                                    keep.push_back(h);
                            if (!keep.empty()) { hits.swap(keep); total = (long long)hits.size(); }
                        }
                        s->page = zhSearch ? 0 : pageIdx;
                        s->total = total;
                        // 中文检索是一次抓齐再本地筛选，远端翻页会跟筛选集对不上，所以只留一页
                        s->pages = zhSearch ? 1 : (total > 0 ? (int)((total + size - 1) / size) : 1);
                        if (s->page >= s->pages) s->page = s->pages - 1;

                        s->list->Clear();
                        for (const auto& h : hits) {
                            auto card = ItemCardClick();
                            auto row = ItemRow(12.f);

                            auto icon = std::make_shared<IconImageView>();
                            icon->SetSource(self->ctx_->icons, h.iconUrl);
                            icon->Tooltip = h.title.empty() ? h.slug : h.title;
                            row->Add(icon);

                            auto col = vcui::VBox(2.f);
                            col->Grow = 1.f;
                            auto title = Text(DisplayTitle(self->ctx_, h), vcui::Type::Body1Strong);
                            title->Ellipsis = true;
                            col->Add(title);
                            auto desc = Caption(h.description);
                            desc->Ellipsis = true;
                            col->Add(desc);
                            col->Add(Caption(WFormat(L"@%s · %s · 下载 %s",
                                                     h.author.c_str(), MrTypeName(h.projectType).c_str(),
                                                     HumanBytes(h.downloads).c_str())));
                            row->Add(col);

                            auto hit = h;
                            card->Clicked = [self, s, hit] { self->ShowDetail(s, hit); };

                            card->Add(row);
                            s->list->Add(card);
                        }
                        if (hits.empty()) s->list->Add(Caption(L"没有找到匹配的项目"));
                        RevealRows(s->list, self->ctx_->settings);

                        if (!zhs.empty() && !hits.empty())
                            s->status->Text = WFormat(L"共 %lld 条结果 · 中文检索命中", s->total);
                        else
                            s->status->Text = WFormat(L"共 %lld 条结果", s->total);
                        self->UpdatePager(s);
                    });
}

// ---------------- 分页 ----------------

void DownloadPage::UpdatePager(const std::shared_ptr<State>& s) {
    if (!s->pageInfo || !s->prev || !s->next) return;
    s->pageInfo->Text = WFormat(L"第 %d / %d 页", s->page + 1, s->pages > 0 ? s->pages : 1);
    s->prev->Enabled = s->page > 0;
    s->next->Enabled = s->page + 1 < s->pages;
}

void DownloadPage::GoPage(const std::shared_ptr<State>& s, int delta) {
    int target = s->page + delta;
    if (target < 0 || target >= s->pages) return;
    if (s->sub == 0) {
        s->page = target;
        RenderVersions(s);
    } else {
        LoadSearch(s, target);
    }
}

// ---------------- 详情页（版本 / 介绍） ----------------

void DownloadPage::ShowDetail(const std::shared_ptr<State>& s, const MrHit& hit) {
    if (!s->body) return;

    if (!s->detail) {
        s->detail = vcui::VBox(12.f);
        s->detail->Grow = 1.f;
        s->detail->Visible = false;
        s->body->Add(s->detail);
    }
    s->detail->Clear();
    s->detail->Visible = true;
    if (s->listScroll) s->listScroll->Visible = false;
    if (s->toolbar) s->toolbar->Visible = false;
    if (s->foot) s->foot->Visible = false;
    size_t gen = ++s->detailGen;

    auto self = this;

    // 顶部：返回 + 图标 + 标题/元信息
    auto head = vcui::HBox(12.f);
    head->Cross = vcui::Stack::Center;

    auto back = std::make_shared<vcui::IconButton>(L"\xE72B");
    back->Tooltip = L"返回列表";
    back->Clicked = [self, s] {
        ++s->detailGen;                     // 丢掉在途回调
        if (s->detail) s->detail->Visible = false;
        if (s->listScroll) s->listScroll->Visible = true;
        if (s->toolbar) s->toolbar->Visible = true;
        if (s->foot) s->foot->Visible = true;
        if (self->ctx_->surface) {
            self->ctx_->surface->Invalidate();
            self->ctx_->surface->PlayAnim();
        }
    };
    head->Add(back);

    auto icon = std::make_shared<IconImageView>();
    icon->size = D2D1::SizeF(44.f, 44.f);
    icon->Radius = 8.f;
    icon->SetSource(ctx_->icons, hit.iconUrl);
    head->Add(icon);

    auto col = vcui::VBox(2.f);
    col->Grow = 1.f;
    auto title = std::make_shared<vcui::Label>(DisplayTitle(ctx_, hit), 20.f, 600);
    title->Ellipsis = true;
    col->Add(title);

    std::wstring sub = L"@";
    sub += hit.author.empty() ? std::wstring(L"unknown") : hit.author;
    if (!hit.title.empty()) sub += L" · " + hit.title;
    sub += L" · " + MrTypeName(hit.projectType);
    sub += L" · 下载 " + HumanBytes(hit.downloads);
    auto meta = Caption(sub);
    meta->Ellipsis = true;
    col->Add(meta);
    head->Add(col);

    // 外链：Modrinth 一定有；汉化表里带 mcmod 编号的再加一个 MC百科
    auto links = vcui::HBox(8.f);
    links->Cross = vcui::Stack::Center;

    auto toModrinth = std::make_shared<vcui::Button>(L"转到 Modrinth");
    toModrinth->Glyph = L"\xE8A7";   // OpenInNewWindow
    toModrinth->Clicked = [hit] { OpenExternal(ModrinthPageUrl(hit)); };
    links->Add(toModrinth);

    const int mcmod = ctx_->wiki ? ctx_->wiki->Mcmod(hit.slug) : 0;
    if (mcmod > 0) {
        auto toMcmod = std::make_shared<vcui::Button>(L"转到 MC百科");
        toMcmod->Glyph = L"\xE8A7";
        toMcmod->Tooltip = L"mcmod.cn/class/" + std::to_wstring(mcmod);
        toMcmod->Clicked = [mcmod] { OpenExternal(McmodPageUrl(mcmod)); };
        links->Add(toMcmod);
    }
    head->Add(links);

    s->detail->Add(head);
    s->detail->Add(std::make_shared<vcui::Divider>());

    auto tabs = std::make_shared<vcui::Segmented>(std::vector<std::wstring>{ L"版本", L"介绍" }, 0);
    // Segmented 在 Stretch 的 VBox 里会被拉满整行，套一层 Start 的 HBox 保持自身宽度
    auto tabRow = vcui::HBox(0.f);
    tabRow->Cross = vcui::Stack::Start;
    tabRow->Add(tabs);
    s->detail->Add(tabRow);

    // 两个页签的内容叠在同一块区域里，用 Visible 切换
    auto pages = vcui::VBox(0.f);
    pages->Grow = 1.f;

    auto vStatus = Caption(L"正在获取版本列表…");
    auto vList = std::make_shared<vcui::VirtualList>();
    vList->RowHeight = 62.f;
    vList->Gap = 6.f;
    vList->Grow = 1.f;
    auto vPage = vcui::VBox(8.f);
    vPage->Grow = 1.f;
    vPage->Add(vStatus);
    vPage->Add(vList);

    auto iStatus = Caption(L"正在获取项目介绍…");
    auto iBox = vcui::VBox(0.f);
    iBox->Grow = 1.f;
    auto iScroll = std::make_shared<vcui::ScrollView>();
    iScroll->Grow = 1.f;
    iScroll->Add(iBox);
    auto iPage = vcui::VBox(8.f);
    iPage->Grow = 1.f;
    iPage->Visible = false;
    iPage->Add(iStatus);
    iPage->Add(iScroll);

    pages->Add(vPage);
    pages->Add(iPage);
    s->detail->Add(pages);

    tabs->Changed = [vPage, iPage](int idx) {
        vPage->Visible = idx == 0;
        iPage->Visible = idx == 1;
    };

    LoadDetailVersions(s, hit, vStatus, vList, gen);
    LoadDetailIntro(s, hit, iStatus, iBox, gen);

    if (ctx_->surface) {
        ctx_->surface->Invalidate();
        ctx_->surface->PlayAnim();
    }
}

void DownloadPage::LoadDetailVersions(const std::shared_ptr<State>& s, const MrHit& hit,
                                      const std::shared_ptr<vcui::Label>& status,
                                      const std::shared_ptr<vcui::VirtualList>& list, size_t gen) {
    auto self = this;
    auto selfType = s->type;
    ctx_->http->Get(MrVersionsUrl(hit.id),
                    [self, s, gen, status, list, selfType](bool ok, const std::string& body,
                                                          const std::string& err) {
                        if (s->detailGen != gen) return;
                        if (!ok) {
                            status->Text = L"获取失败：" + Utf8ToWide(err);
                            return;
                        }
                        std::vector<MrFile> files;
                        std::string pe;
                        if (!ParseMrVersions(body, files, &pe)) {
                            status->Text = L"解析失败：" + Utf8ToWide(pe);
                            return;
                        }
                        status->Text = WFormat(L"%zu 个文件（按发布时间倒序）", files.size());

                        // 版本列表动辄上千条：交给 VirtualList，只建视口内那几行，滚走复用
                        auto filesPtr = std::make_shared<std::vector<MrFile>>(std::move(files));
                        list->Reset();
                        list->Build = [filesPtr](int) {
                            auto r = std::make_shared<VersionRow>();
                            r->Shadow = false;
                            r->Border = true;
                            r->Radius = 6.f;
                            r->Pad(12.f);
                            auto row = vcui::HBox(10.f);
                            row->Cross = vcui::Stack::Center;
                            auto col = vcui::VBox(2.f);
                            col->Grow = 1.f;
                            r->name = Text(L"", vcui::Type::Body1Strong);
                            r->name->Ellipsis = true;
                            col->Add(r->name);
                            r->meta = Caption(L"");
                            r->meta->Ellipsis = true;
                            col->Add(r->meta);
                            row->Add(col);
                            r->size = Caption(L"");
                            row->Add(r->size);
                            r->btn = std::make_shared<vcui::Button>(L"下载");
                            r->btn->W = 76.f;
                            row->Add(r->btn);
                            r->Add(row);
                            return r;
                        };
                        list->Bind = [filesPtr, self, selfType](vcui::Widget& w, int i) {
                            auto* r = dynamic_cast<VersionRow*>(&w);
                            if (!r || i < 0 || i >= (int)filesPtr->size()) return false;
                            const MrFile& f = (*filesPtr)[(size_t)i];
                            r->name->Text = f.name;
                            r->meta->Text = WFormat(L"%s · %s · %s", f.versionName.c_str(),
                                                    f.loaders.c_str(), f.gameVersions.c_str());
                            r->size->Text = HumanBytes(f.size);
                            auto file = f;
                            r->btn->Clicked = [self, selfType, file] {
                                std::wstring root = CurrentRootPath(*self->ctx_->settings);
                                std::wstring path = JoinPath(JoinPath(root, MrTargetFolder(selfType)), file.name);
                                if (self->ctx_->dl->Enqueue(file.url, path, file.sha1, file.size))
                                    self->ctx_->Toast(L"已加入下载：" + file.name, vcui::InfoTone::Success);
                                else
                                    self->ctx_->Toast(L"入队失败：" + file.name, vcui::InfoTone::Danger);
                            };
                            return true;
                        };
                        list->ItemCount = (int)filesPtr->size();
                        if (self->ctx_->settings->animations) list->AnimateIn();
                    });
}
void DownloadPage::LoadDetailIntro(const std::shared_ptr<State>& s, const MrHit& hit,
                                   const std::shared_ptr<vcui::Label>& status,
                                   const std::shared_ptr<vcui::Stack>& holder, size_t gen) {
    auto self = this;
    auto fallback = hit.description;
    ctx_->http->Get(MrProjectUrl(hit.id),
                    [self, s, gen, status, holder, fallback](bool ok, const std::string& body,
                                                             const std::string& err) {
                        if (s->detailGen != gen) return;
                        if (!ok) {
                            status->Text = L"介绍获取失败：" + Utf8ToWide(err);
                            return;
                        }
                        MrProject p;
                        std::string pe;
                        if (!ParseMrProject(body, p, &pe)) {
                            status->Text = L"介绍解析失败：" + Utf8ToWide(pe);
                            return;
                        }
                        std::wstring md = p.bodyMd.empty() ? fallback : p.bodyMd;
                        auto blocks = ParseMarkdown(md);
                        holder->Clear();
                        if (blocks.empty()) {
                            holder->Add(Caption(L"这个项目没有填写 Markdown 介绍。"));
                        } else if (self->ctx_->surface) {
                            holder->Add(BuildMarkdown(blocks, self->ctx_->surface->Th().t));
                        }
                        std::wstring info = WFormat(L"%zu 段介绍", blocks.size());
                        if (!p.license.empty()) info += L" · 许可证 " + p.license;
                        if (!p.categories.empty()) {
                            info += L" · ";
                            for (size_t i = 0; i < p.categories.size(); ++i) {
                                if (i) info += L" / ";
                                info += p.categories[i];
                            }
                        }
                        status->Text = info;
                    });
}

} // namespace vcl
