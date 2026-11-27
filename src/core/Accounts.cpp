#include "core/Accounts.h"

#include <windows.h>
#include <wincrypt.h>

#include <cwctype>

#include "core/Text.h"

namespace {

void HexByte(std::wstring& out, unsigned char b) {
    static const wchar_t* d = L"0123456789abcdef";
    out.push_back(d[(b >> 4) & 0xF]);
    out.push_back(d[b & 0xF]);
}

} // namespace

std::wstring OfflineUuidOf(const std::wstring& name) {
    const std::string seed = "OfflinePlayer:" + WideToUtf8(name);
    unsigned char md5[16]{};
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
            CryptHashData(hash, (const BYTE*)seed.data(), (DWORD)seed.size(), 0);
            DWORD len = 16;
            CryptGetHashParam(hash, HP_HASHVAL, md5, &len, 0);
            CryptDestroyHash(hash);
        }
        CryptReleaseContext(prov, 0);
    }
    md5[6] = (unsigned char)((md5[6] & 0x0F) | 0x30);   // version 3
    md5[8] = (unsigned char)((md5[8] & 0x3F) | 0x80);   // RFC variant

    std::wstring s;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) s.push_back(L'-');
        HexByte(s, md5[i]);
    }
    return s;
}

bool ValidPlayerName(const std::wstring& name) {
    if (name.size() < 3 || name.size() > 16) return false;
    for (wchar_t c : name) {
        if (!(iswalnum(c) || c == L'_')) return false;
    }
    return true;
}

std::wstring DashUuid(const std::wstring& id32) {
    if (id32.size() != 32) return id32;
    return id32.substr(0, 8) + L"-" + id32.substr(8, 4) + L"-" + id32.substr(12, 4) + L"-" +
           id32.substr(16, 4) + L"-" + id32.substr(20, 12);
}

std::wstring UndashUuid(const std::wstring& dashed) {
    std::wstring s;
    for (wchar_t c : dashed) {
        if (c != L'-') s.push_back(c);
    }
    return s;
}

int FindAccount(const Settings& s, const std::wstring& type, const std::wstring& name) {
    for (size_t i = 0; i < s.accounts.size(); ++i) {
        if (s.accounts[i].type == type && s.accounts[i].name == name) return (int)i;
    }
    return -1;
}

int AddAccount(Settings& s, AccountEntry e) {
    // 同类型同名字的旧条目先清掉，等于"重新登录会刷新令牌"
    for (size_t i = 0; i < s.accounts.size();) {
        if (s.accounts[i].type == e.type && s.accounts[i].name == e.name) s.accounts.erase(s.accounts.begin() + i);
        else ++i;
    }
    s.accounts.push_back(std::move(e));
    s.currentAccount = (int)s.accounts.size() - 1;
    s.accountName = s.accounts.back().name;
    return s.currentAccount;
}

bool RemoveAccount(Settings& s, int index) {
    if (index < 0 || index >= (int)s.accounts.size()) return false;
    s.accounts.erase(s.accounts.begin() + index);
    if (s.accounts.empty()) {
        s.currentAccount = 0;
        return true;
    }
    if (index < s.currentAccount) s.currentAccount--;
    if (s.currentAccount >= (int)s.accounts.size()) s.currentAccount = (int)s.accounts.size() - 1;
    s.accountName = s.accounts[(size_t)s.currentAccount].name;
    return true;
}

bool SetCurrentAccount(Settings& s, int index) {
    if (index < 0 || index >= (int)s.accounts.size()) return false;
    s.currentAccount = index;
    s.accountName = s.accounts[(size_t)index].name;
    return true;
}

const AccountEntry* CurrentAccount(const Settings& s) {
    if (s.accounts.empty()) return nullptr;
    if (s.currentAccount < 0 || s.currentAccount >= (int)s.accounts.size()) return nullptr;
    return &s.accounts[(size_t)s.currentAccount];
}
