#include "core/ModWiki.h"

#include "core/Text.h"

#include <fstream>
#include <iterator>

namespace vcl {

namespace {

// 从 pos 起找 key，返回其字符串值；找不到返回 false。
bool ReadField(const std::wstring& src, size_t& pos, const wchar_t* key, std::wstring& out) {
    std::wstring pat = L"\"" + std::wstring(key) + L"\"";
    size_t k = src.find(pat, pos);
    if (k == std::wstring::npos) return false;
    // key 的收尾引号之后，值的开引号是下一个引号（这几个字段都是字符串）
    size_t i = k + pat.size();
    while (i < src.size() && src[i] != L'"') ++i;
    if (i >= src.size()) return false;
    out.clear();
    for (size_t j = i + 1; j < src.size(); ++j) {
        wchar_t c = src[j];
        if (c == L'"') { pos = j + 1; return true; }
        if (c != L'\\') { out.push_back(c); continue; }
        if (j + 1 >= src.size()) return false;
        wchar_t e = src[++j];
        switch (e) {
        case L'"': out.push_back(L'"'); break;
        case L'\\': out.push_back(L'\\'); break;
        case L'/': out.push_back(L'/'); break;
        case L'b': out.push_back(L'\b'); break;
        case L'f': out.push_back(L'\f'); break;
        case L'n': out.push_back(L'\n'); break;
        case L'r': out.push_back(L'\r'); break;
        case L't': out.push_back(L'\t'); break;
        case L'u': {
            // \uXXXX：中文名里基本不会出现，按 UTF-16 码位直接吃掉 4 位十六进制
            unsigned int cp = 0;
            for (int k2 = 0; k2 < 4 && j + 1 < src.size(); ++k2) {
                wchar_t h = src[++j];
                cp <<= 4;
                if (h >= L'0' && h <= L'9') cp |= (unsigned int)(h - L'0');
                else if (h >= L'a' && h <= L'f') cp |= (unsigned int)(h - L'a' + 10);
                else if (h >= L'A' && h <= L'F') cp |= (unsigned int)(h - L'A' + 10);
            }
            out.push_back((wchar_t)cp);
            break;
        }
        default: out.push_back(e); break;
        }
    }
    return false;
}

// "更多箱子 (Iron Chests)" → "Iron Chests"；没有括号就返回空
std::wstring ExtractEn(const std::wstring& zhcn) {
    size_t lp = zhcn.rfind(L'(');
    if (lp == std::wstring::npos) lp = zhcn.rfind(L'（');
    if (lp == std::wstring::npos) return L"";
    size_t rp = zhcn.find(L')', lp);
    if (rp == std::wstring::npos) rp = zhcn.find(L'）', lp);
    if (rp == std::wstring::npos) return L"";
    std::wstring s = zhcn.substr(lp + 1, rp - lp - 1);
    return Trim(s);
}

} // namespace

bool ModWiki::Load(const std::wstring& path) {
    entries_.clear();
    bySlug_.clear();

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;

    std::wstring src = Utf8ToWide(bytes);
    if (src.empty()) return false;

    // 文件是固定格式的数组 [{slug, mcmod, zhcn}, ...]，按字段顺序扫即可，不引第三方 JSON
    size_t pos = 0;
    while (true) {
        std::wstring slug;
        if (!ReadField(src, pos, L"slug", slug)) break;
        size_t afterSlug = pos;
        std::wstring zh;
        if (!ReadField(src, afterSlug, L"zhcn", zh)) {
            pos = afterSlug;
            continue;
        }
        pos = afterSlug;
        if (slug.empty() || zh.empty()) continue;

        Entry e;
        e.slug = slug;
        e.zhcn = zh;
        e.en = ExtractEn(zh);
        bySlug_[ToLower(slug)] = entries_.size();
        entries_.push_back(std::move(e));
    }
    return !entries_.empty();
}

const std::wstring* ModWiki::Zh(const std::wstring& slug) const {
    auto it = bySlug_.find(ToLower(slug));
    if (it == bySlug_.end()) return nullptr;
    return &entries_[it->second].zhcn;
}

bool ModWiki::HasCjk(const std::wstring& s) {
    for (wchar_t c : s) {
        if ((c >= 0x4E00 && c <= 0x9FFF) ||     // 基本区
            (c >= 0x3400 && c <= 0x4DBF) ||     // 扩展 A
            (c >= 0xF900 && c <= 0xFAFF))       // 兼容表意
            return true;
    }
    return false;
}

void ModWiki::ResolveZh(const std::wstring& query, std::vector<std::wstring>& slugs,
                        std::wstring& englishOut) const {
    slugs.clear();
    englishOut.clear();
    std::wstring q = Trim(query);
    if (q.empty()) return;
    std::wstring ql = ToLower(q);
    bool needEn = true;

    for (const auto& e : entries_) {
        bool hit = e.zhcn.find(q) != std::wstring::npos;
        if (!hit && !e.en.empty()) hit = ToLower(e.en).find(ql) != std::wstring::npos;
        if (!hit) continue;
        if (slugs.size() < 40) slugs.push_back(e.slug);
        if (needEn && !e.en.empty()) {
            englishOut = e.en;
            needEn = false;
        }
    }
}

} // namespace vcl

