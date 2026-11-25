#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace vcl {

// ModWiki.json（author 提供的 MC 百科对照表）：slug → 中文名。
// 两个用途：
//   1) 汉化：Modrinth 只给英文标题，这里给卡片补一个中文名；
//   2) 中文搜索：中文查询先在这张表里命中，再拿条目自带的英文名去查 Modrinth。
class ModWiki {
public:
    bool Load(const std::wstring& path);
    // 从 exe 里打包的 resource/modwiki/ModWiki.json 读
    bool LoadEmbedded();
    bool Loaded() const { return !entries_.empty(); }
    size_t Size() const { return entries_.size(); }

    // slug → 中文名（比如 "industrial-craft" → "工业时代2 (Industrial Craft 2)"）
    const std::wstring* Zh(const std::wstring& slug) const;

    // slug → MC 百科的 class 编号（mcmod.cn/class/<id>）；没有这条或值为空返回 0
    int Mcmod(const std::wstring& slug) const;

    // 查询串里有没有中文（CJK 统一表意 / 扩展 A / 兼容区）
    static bool HasCjk(const std::wstring& s);

    // 中文查询 → 命中的 slug 列表 + 给远端用的英文词（取自中文名后的括号，如 (Iron Chests)）
    void ResolveZh(const std::wstring& query, std::vector<std::wstring>& slugs,
                   std::wstring& englishOut) const;

private:
    bool LoadBytes(const std::string& bytes);

    struct Entry {
        std::wstring slug;
        std::wstring zhcn;
        std::wstring en;   // 括号里的英文名，可能为空
        int mcmod = 0;     // MC 百科 class 编号，0 表示没有
    };
    std::vector<Entry> entries_;
    std::unordered_map<std::wstring, size_t> bySlug_;   // 小写 slug → 下标
    std::unordered_map<std::wstring, int> mcmodBySlug_; // 小写 slug → mcmod（含 zhcn 为 null 的条目）
};

} // namespace vcl

