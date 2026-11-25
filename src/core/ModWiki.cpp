#include "core/ModWiki.h"

#include "core/Res.h"
#include "core/Text.h"

#include <fstream>
#include <iterator>

namespace vcl {

namespace {

bool IsSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r'; }

// 从 pos 起找 key，返回其字符串值；找不到或值不是字符串（null / 数字）返回 false。
// 注意：表里有大量 "zhcn": null，不能把 null 之后的引号当值的开引号。
bool ReadField(const std::wstring& src, size_t& pos, const wchar_t* key, std::wstring& out) {
    std::wstring pat = L"\"" + std::wstring(key) + L"\"";
    size_t k = src.find(pat, pos);
    if (k == std::wstring::npos) return false;
    size_t i = k + pat.size();
    while (i < src.size() && IsSpace(src[i])) ++i;
    if (i >= src.size() || src[i] != L':') return false;
    ++i;
    while (i < src.size() && IsSpace(src[i])) ++i;
    if (i >= src.size()) return false;
    if (src[i] != L'"') {
        // null / 数字 / true 之类：只把游标挪到值开头，让调用方接着往下扫
        pos = i;
        return false;
    }
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
    return LoadBytes(bytes);
}

bool ModWiki::LoadEmbedded() {
    entries_.clear();
    bySlug_.clear();
    const void* data = nullptr;
    unsigned size = 0;
    if (!EmbeddedResource(L"modwiki/ModWiki.json", &data, &size) || !data || size == 0) return false;
    std::string bytes(static_cast<const char*>(data), static_cast<size_t>(size));
    return LoadBytes(bytes);
}

bool ModWiki::LoadBytes(const std::string& bytes) {
    if (bytes.empty()) return false;

    std::wstring src = Utf8ToWide(bytes);
    if (src.empty()) return false;

    // 文件是固定格式的数组 [{slug, mcmod, zhcn}, ...]。逐条按大括号切出来，
    // 每条只在自身范围内找字段——zhcn 常是 null，跨条扫描会读到下一条的键名。
    size_t pos = 0;
    while (true) {
        size_t begin = src.find(L'{', pos);
        if (begin == std::wstring::npos) break;
        size_t end = src.find(L'}', begin);
        if (end == std::wstring::npos) break;
        const std::wstring rec = src.substr(begin, end - begin);
        pos = end + 1;

        size_t q = 0;
        std::wstring slug, zh;
        if (!ReadField(rec, q, L"slug", slug)) continue;
        q = 0;
        if (!ReadField(rec, q, L"zhcn", zh)) continue;   // zhcn 为 null 的条目不收
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

