#include "ui/DownloadPage.h"
#include "ui/IconImage.h"
#include "core/Paths.h"
#include "core/ModWiki.h"
#include "core/Text.h"

#include <fstream>
#include <cstdlib>
#include <algorithm>

namespace vcl {

struct DownloadPage::State {
    std::shared_ptr<vcui::Stack> list;
    std::shared_ptr<vcui::Label> status;
    std::shared_ptr<vcui::Input> query;
    std::shared_ptr<vcui::Dropdown> mirror;
    std::shared_ptr<vcui::Dropdown> order;
    std::shared_ptr<vcui::Label> pageInfo;
    std::shared_ptr<vcui::Button> prev;
    std::shared_ptr<vcui::Button> next;

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

std::shared_ptr<vcui::Stack> ItemRow(float gap = 10.f) {
    auto r = vcui::HBox(gap);
    r->Cross = vcui::Stack::Center;
    return r;
}

// 列表是异步回来才填的，构建期的 StaggerIn 覆盖不到，所以每次填完自己补一遍错峰入场
void RevealRows(const std::shared_ptr<vcui::Stack>& list, const Settings* st) {
    if (!list || (st && !st->animations)) return;
    float d = 0.f;
    for (auto& k : list->Children()) {
        k->FadeIn(.20f, 8.f, d);
        d = (std::min)(d + .025f, .16f);
    }
}

// 版本类型 → resource\image 里的方块图标
std::wstring VersionIconPath(const std::wstring& type) {
    std::wstring name = L"Grass_Block.png";                  // release：草方块
    if (type == L"snapshot") name = L"Command_Block.png";     // snapshot：命令方块
    else if (type == L"old_beta" || type == L"old_alpha") name = L"StoneOldBeta.png";
    return JoinPath(JoinPath(ResourceDir(), L"image"), name);
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

    // 底部：状态 + 分页
    auto foot = vcui::HBox(8.f);
    foot->Add(s->status);
    foot->Add(s->prev);
    foot->Add(s->pageInfo);
    foot->Add(s->next);
    page->Add(foot);

    auto self = this;
    s->prev->Clicked = [self, s] { self->GoPage(s, -1); };
    s->next->Clicked = [self, s] { self->GoPage(s, +1); };
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

    s->list = vcui::VBox(6.f);
    auto scroll = std::make_shared<vcui::ScrollView>();
    scroll->Grow = 1.f;
    scroll->ShowBar = true;
    scroll->Add(s->list);
    page.Add(scroll);

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
        s->page = 0;
        self->RenderVersions(s);
    };

    LoadManifest(s);
}

void DownloadPage::RenderVersions(const std::shared_ptr<State>& s) {
    std::wstring key = ToLower(Trim(s->filter));
    std::vector<const McVersion*> all;
    all.reserve(s->versions.size());
    for (const auto& v : s->versions) {
        if (!key.empty() && ToLower(v.id).find(key) == std::wstring::npos) continue;
        all.push_back(&v);
    }

    int size = PageSizeFor(s->sub);
    s->total = (long long)all.size();
    s->pages = s->total > 0 ? (int)((s->total + size - 1) / size) : 1;
    if (s->page >= s->pages) s->page = s->pages - 1;
    if (s->page < 0) s->page = 0;

    s->list->Clear();
    int from = s->page * size;
    int to = from + size;
    if (to > (int)all.size()) to = (int)all.size();

    auto self = this;
    for (int i = from; i < to; ++i) {
        const McVersion& v = *all[i];
        auto card = ItemCard();
        auto row = ItemRow(10.f);

        // 版本图标（resource\image 自带）：正式版草方块、快照命令方块、远古版本旧石头
        auto icon = std::make_shared<IconImageView>();
        icon->size = D2D1::SizeF(32.f, 32.f);
        icon->Radius = 5.f;
        icon->SetSource(ctx_->icons, L"file:" + VersionIconPath(v.type));
        row->Add(icon);

        auto b = std::make_shared<vcui::Badge>(v.type,
                                               v.type == L"release" ? vcui::Badge::Accent : vcui::Badge::Neutral);
        b->W = 76.f;
        row->Add(b);

        auto name = Text(v.id, vcui::Type::Body1Strong);
        name->Grow = 1.f;
        name->Ellipsis = true;
        row->Add(name);
        row->Add(Caption(IsoDateOnly(v.releaseTime)));

        auto btn = std::make_shared<vcui::Button>(L"安装");
        btn->W = 76.f;
        auto ver = v;
        btn->Clicked = [self, s, ver] { self->InstallGame(s, ver); };
        row->Add(btn);

        card->Add(row);
        s->list->Add(card);
    }
    if (all.empty()) s->list->Add(Caption(L"没有匹配的版本"));
    RevealRows(s->list, ctx_->settings);

    s->status->Text = WFormat(L"%s%lld 个版本", key.empty() ? L"" : L"筛选结果 ", s->total);
    UpdatePager(s);
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
        s->status->Text = WFormat(L"%zu 个版本（%s）", s->versions.size(),
                                  bmcl ? L"BMCLAPI" : L"Mojang");
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

    s->list = vcui::VBox(6.f);
    auto scroll = std::make_shared<vcui::ScrollView>();
    scroll->Grow = 1.f;
    scroll->ShowBar = true;
    scroll->Add(s->list);
    page.Add(scroll);

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
                            auto card = ItemCard();
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

                            auto btn = std::make_shared<vcui::Button>(L"版本");
                            btn->W = 76.f;
                            auto hit = h;
                            auto type = s->type;
                            auto self2 = self;
                            btn->Clicked = [self2, hit, type] { self2->ShowVersions(hit, type); };
                            row->Add(btn);

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

// ---------------- 版本文件面板 ----------------

void DownloadPage::ShowVersions(const MrHit& hit, const std::wstring& type) {
    auto host = ctx_->surface;
    if (!host) return;

    std::shared_ptr<vcui::Stack> holder;
    auto card = OpenPanel(host, hit.title.empty() ? hit.slug : hit.title, 640.f, 480.f, nullptr, &holder);
    if (!card || !holder) return;

    auto status = Caption(L"正在获取版本列表…");
    holder->Add(status);

    auto list = vcui::VBox(6.f);
    auto scroll = std::make_shared<vcui::ScrollView>();
    scroll->Grow = 1.f;
    scroll->Add(list);
    holder->Add(scroll);

    auto self = this;
    auto selfType = type;
    ctx_->http->Get(MrVersionsUrl(hit.id),
                    [self, selfType, status, list](bool ok, const std::string& body, const std::string& err) {
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
                        list->Clear();
                        for (const auto& f : files) {
                            auto rowCard = ItemCard();
                            auto row = ItemRow(10.f);

                            auto col = vcui::VBox(2.f);
                            col->Grow = 1.f;
                            auto name = Text(f.name, vcui::Type::Body1Strong);
                            name->Ellipsis = true;
                            col->Add(name);
                            col->Add(Caption(WFormat(L"%s · %s · %s", f.versionName.c_str(),
                                                     f.loaders.c_str(), f.gameVersions.c_str())));
                            row->Add(col);
                            row->Add(Caption(HumanBytes(f.size)));

                            auto btn = std::make_shared<vcui::Button>(L"下载");
                            btn->W = 76.f;
                            auto file = f;
                            auto self2 = self;
                            btn->Clicked = [self2, file, selfType] {
                                std::wstring root = CurrentRootPath(*self2->ctx_->settings);
                                std::wstring path = JoinPath(JoinPath(root, MrTargetFolder(selfType)), file.name);
                                if (self2->ctx_->dl->Enqueue(file.url, path, file.sha1, file.size))
                                    self2->ctx_->Toast(L"已加入下载：" + file.name, vcui::InfoTone::Success);
                                else
                                    self2->ctx_->Toast(L"入队失败：" + file.name, vcui::InfoTone::Danger);
                            };
                            row->Add(btn);

                            rowCard->Add(row);
                            list->Add(rowCard);
                        }
                        RevealRows(list, self->ctx_->settings);
                    });
}

} // namespace vcl


