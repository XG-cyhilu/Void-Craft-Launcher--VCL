#include "ui/DownloadPage.h"
#include "core/Paths.h"
#include "core/Text.h"

#include <fstream>

namespace vcl {

struct DownloadPage::State {
    std::shared_ptr<vcui::Stack> list;
    std::shared_ptr<vcui::Label> status;
    std::shared_ptr<vcui::Input> query;
    std::shared_ptr<vcui::Dropdown> mirror;
    std::shared_ptr<vcui::Label> more;       // "加载更多" 的计数提示
    std::vector<McVersion> versions;

    int sub = 0;
    std::wstring type;
    std::wstring index = L"relevance";
    int offset = 0;
    size_t gen = 0;                          // 丢弃过期回调
    bool loading = false;
    bool bmclOk = true;                      // BMCLAPI 本机是否可达（不可达时回退官方并在状态里说明）
};

std::vector<std::wstring> DownloadPage::SubNav() {
    return { L"游戏", L"模组", L"整合包", L"资源包", L"光影包", L"数据包" };
}

DownloadPage::DownloadPage(UiCtx* ctx) : ctx_(ctx) {}

static std::shared_ptr<vcui::Widget> MakeListCard(std::shared_ptr<vcui::Stack>& list) {
    list = vcui::VBox(0.f);
    auto scroll = std::make_shared<vcui::ScrollView>();
    scroll->Grow = 1.f;
    scroll->ShowBar = true;
    scroll->Add(list);

    auto card = std::make_shared<vcui::Card>();
    card->Grow = 1.f;
    card->Add(scroll);
    return card;
}

std::shared_ptr<vcui::Widget> DownloadPage::Build(int sub) {
    auto page = vcui::VBox(12.f);
    page->Grow = 1.f;

    auto s = std::make_shared<State>();
    s->sub = sub;
    s->status = Caption(L"");

    if (sub == 0) {
        page->Add(PageHeader(L"下载游戏",
                             L"版本清单来自 BMCLAPI 镜像或 Mojang 官方源；安装会把版本 json 与 client.jar 落到 versions/ 下。"));
        BuildGame(s, *page);
    } else {
        static const wchar_t* types[] = { L"", L"mod", L"modpack", L"resourcepack", L"shader", L"datapack" };
        s->type = types[sub < 6 ? sub : 1];
        page->Add(PageHeader(L"下载" + MrTypeName(s->type),
                             L"数据源仅支持 Modrinth，文件会落到当前游戏目录对应的子文件夹。"));
        BuildModrinth(s, *page);
    }

    s->status->Grow = 1.f;
    page->Add(s->status);
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

    auto refresh = std::make_shared<vcui::Button>(L"刷新清单");
    auto barSp = vcui::Spacer(0.f);
    barSp->Grow = 1.f;

    bar->Add(s->mirror);
    bar->Add(s->query);
    bar->Add(refresh);
    bar->Add(barSp);
    page.Add(bar);

    page.Add(MakeListCard(s->list));

    auto self = this;
    s->mirror->Changed = [self, s](int idx) {
        self->ctx_->settings->mirror = idx == 0 ? L"bmclapi" : L"mojang";
        if (self->ctx_->saveSettings) self->ctx_->saveSettings();
        self->LoadManifest(s);
    };
    refresh->Clicked = [self, s] { self->LoadManifest(s); };
    s->query->Changed = [self, s](const std::wstring& q) { self->RenderVersions(s, q); };

    LoadManifest(s);
}

void DownloadPage::RenderVersions(const std::shared_ptr<State>& s, const std::wstring& filter) {
    s->list->Clear();
    std::wstring key = ToLower(Trim(filter));
    size_t shown = 0;
    for (const auto& v : s->versions) {
        if (!key.empty() && ToLower(v.id).find(key) == std::wstring::npos) continue;
        auto row = vcui::HBox(10.f);
        row->Pad(12.f);

        auto b = std::make_shared<vcui::Badge>(v.type,
                                               v.type == L"release" ? vcui::Badge::Accent : vcui::Badge::Neutral);
        b->W = 76.f;
        row->Add(b);

        auto name = Text(v.id, vcui::Type::Body1Strong);
        name->Grow = 1.f;
        row->Add(name);
        row->Add(Caption(IsoDateOnly(v.releaseTime)));

        auto btn = std::make_shared<vcui::Button>(L"安装");
        btn->W = 76.f;
        auto ver = v;
        auto self = this;
        btn->Clicked = [self, s, ver] { self->InstallGame(s, ver); };
        row->Add(btn);

        s->list->Add(row);
        s->list->Add(std::make_shared<vcui::Divider>());
        if (++shown >= 200) break;
    }
    if (s->versions.empty()) s->status->Text = L"清单为空";
    else s->status->Text = WFormat(L"显示 %zu / %zu 个版本", shown, s->versions.size());
}

void DownloadPage::LoadManifest(const std::shared_ptr<State>& s) {
    s->status->Text = L"正在获取版本清单…";
    bool bmcl = s->mirror ? s->mirror->Selected == 0 : true;
    size_t gen = ++s->gen;
    auto self = this;
    // 镜像只留 8s 探测窗口：国内网络经常直接 reset，等 25s 才回退体验太差
    ctx_->http->Get(McManifestUrl(bmcl), [self, s, bmcl, gen](bool ok, const std::string& body, const std::string& err) {
        if (s->gen != gen) return;
        if (!ok) {
            if (bmcl) {
                // 镜像不可达（企业网/运营商拦截很常见）：自动回退官方源
                s->bmclOk = false;
                if (s->mirror) s->mirror->Selected = 1;
                s->status->Text = L"BMCLAPI 不可达，已自动回退到 Mojang 官方源…";
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
        self->RenderVersions(s, s->query ? s->query->Text : L"");
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
        // 主 URL 按当前可达的源，另一个源作为失败回退
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
    s->query->W = 380.f;

    auto order = std::make_shared<vcui::Dropdown>(
        std::vector<std::wstring>{ L"相关度", L"下载量", L"关注数" }, 0);
    order->W = 128.f;

    auto go = std::make_shared<vcui::Button>(L"搜索", true);
    auto more = std::make_shared<vcui::Button>(L"加载更多");
    auto barSp = vcui::Spacer(0.f);
    barSp->Grow = 1.f;

    bar->Add(s->query);
    bar->Add(order);
    bar->Add(go);
    bar->Add(barSp);
    bar->Add(more);
    page.Add(bar);

    page.Add(MakeListCard(s->list));

    auto self = this;
    order->Changed = [self, s](int idx) {
        s->index = idx == 0 ? L"relevance" : (idx == 1 ? L"downloads" : L"follows");
        s->offset = 0;
        self->LoadSearch(s, 0);
    };
    go->Clicked = [self, s] {
        s->offset = 0;
        self->LoadSearch(s, 0);
    };
    s->query->Submitted = [self, s] {
        s->offset = 0;
        self->LoadSearch(s, 0);
    };
    more->Clicked = [self, s] { self->LoadSearch(s, s->offset); };

    LoadSearch(s, 0);
}

void DownloadPage::LoadSearch(const std::shared_ptr<State>& s, int offset) {
    s->status->Text = offset == 0 ? L"正在查询 Modrinth…" : L"正在加载更多…";
    size_t gen = ++s->gen;
    if (offset == 0) s->list->Clear();
    auto self = this;
    ctx_->http->Get(MrSearchUrl(s->type, s->query ? s->query->Text : L"", s->index, 24, offset),
                    [self, s, gen, offset](bool ok, const std::string& body, const std::string& err) {
                        if (s->gen != gen) return;
                        if (!ok) {
                            s->status->Text = L"Modrinth 查询失败：" + Utf8ToWide(err);
                            return;
                        }
                        std::vector<MrHit> hits;
                        std::string pe;
                        if (!ParseMrSearch(body, hits, &pe)) {
                            s->status->Text = L"响应解析失败：" + Utf8ToWide(pe);
                            return;
                        }
                        s->offset = offset + (int)hits.size();
                        for (const auto& h : hits) {
                            auto row = vcui::HBox(12.f);
                            row->Pad(12.f);

                            auto col = vcui::VBox(2.f);
                            col->Grow = 1.f;
                            col->Add(Text(h.title.empty() ? h.slug : h.title, vcui::Type::Body1Strong));
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

                            s->list->Add(row);
                            s->list->Add(std::make_shared<vcui::Divider>());
                        }
                        if (offset == 0 && hits.empty())
                            s->list->Add(Caption(L"没有找到匹配的项目"));
                        s->status->Text = WFormat(L"已加载 %d 条", s->offset);
                    });
}

void DownloadPage::ShowVersions(const MrHit& hit, const std::wstring& type) {
    auto host = ctx_->surface;
    if (!host) return;

    std::shared_ptr<vcui::Stack> holder;
    auto card = OpenPanel(host, hit.title.empty() ? hit.slug : hit.title, 640.f, 480.f, nullptr, &holder);
    if (!card || !holder) return;

    auto status = Caption(L"正在获取版本列表…");
    holder->Add(status);

    auto list = vcui::VBox(0.f);
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
                            auto row = vcui::HBox(10.f);
                            row->Pad(10.f);

                            auto col = vcui::VBox(2.f);
                            col->Grow = 1.f;
                            col->Add(Text(f.name, vcui::Type::Body1Strong));
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

                            list->Add(row);
                            list->Add(std::make_shared<vcui::Divider>());
                        }
                    });
}

} // namespace vcl




