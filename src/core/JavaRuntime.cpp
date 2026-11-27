#include "core/JavaRuntime.h"
#include "core/Paths.h"
#include "core/Text.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <map>

namespace {

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring Unquote(const std::wstring& s) {
    std::wstring t = Trim(s);
    if (t.size() >= 2 && t.front() == L'"' && t.back() == L'"') t = t.substr(1, t.size() - 2);
    return t;
}

bool EndsWithNoCase(const std::wstring& s, const std::wstring& tail) {
    if (tail.size() > s.size()) return false;
    return Lower(s.substr(s.size() - tail.size())) == Lower(tail);
}

// release 文件是 UTF-8 的 KEY="value" 行
bool ReadRelease(const std::wstring& home, std::map<std::wstring, std::wstring>& kv) {
    std::ifstream f(home + L"\\release", std::ios::binary);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[Utf8ToWide(line.substr(0, eq))] = Utf8ToWide(line.substr(eq + 1));
    }
    return !kv.empty();
}

// 用 PE 头判断位数：0x8664 = x64，0x14C = x86
bool PeIsX64(const std::wstring& exe, bool& isX64) {
    std::ifstream f(exe, std::ios::binary);
    if (!f) return false;
    unsigned char dos[64]{};
    f.read((char*)dos, 64);
    if (f.gcount() < 64 || dos[0] != 'M' || dos[1] != 'Z') return false;
    long off = *(int*)(dos + 0x3C);
    f.seekg(off);
    unsigned char nt[6]{};
    f.read((char*)nt, 6);
    if (f.gcount() < 6 || nt[0] != 'P' || nt[1] != 'E') return false;
    unsigned short machine = *(unsigned short*)(nt + 4);
    if (machine == 0x8664) { isX64 = true; return true; }
    if (machine == 0x014C) { isX64 = false; return true; }
    return false;
}

std::wstring VendorOf(const std::wstring& implementor, const std::wstring& product) {
    const std::wstring l = Lower(implementor + L" " + product);
    if (l.find(L"adoptium") != std::wstring::npos || l.find(L"temurin") != std::wstring::npos)
        return L"Eclipse Temurin";
    if (l.find(L"microsoft") != std::wstring::npos) return L"Microsoft";
    if (l.find(L"azul") != std::wstring::npos) return L"Azul Zulu";
    if (l.find(L"bellsoft") != std::wstring::npos || l.find(L"liberica") != std::wstring::npos)
        return L"BellSoft Liberica";
    if (l.find(L"amazon") != std::wstring::npos || l.find(L"corretto") != std::wstring::npos)
        return L"Amazon Corretto";
    if (l.find(L"ibm") != std::wstring::npos || l.find(L"semeru") != std::wstring::npos)
        return L"IBM Semeru";
    if (l.find(L"alibaba") != std::wstring::npos || l.find(L"dragonwell") != std::wstring::npos)
        return L"Alibaba Dragonwell";
    if (l.find(L"jetbrains") != std::wstring::npos) return L"JetBrains Runtime";
    if (l.find(L"oracle") != std::wstring::npos) return L"Oracle";
    if (implementor.empty()) return L"OpenJDK";
    return implementor;
}

// 目录下（限制层数）找 bin\java.exe
void ScanDirTree(const std::wstring& dir, int depth, const std::wstring& source,
                 std::vector<std::pair<std::wstring, std::wstring>>& hits) {
    if (depth < 0 || !DirExists(dir)) return;
    if (FileExists(dir + L"\\bin\\javaw.exe")) { hits.push_back({ dir + L"\\bin\\javaw.exe", source }); return; }
    if (FileExists(dir + L"\\bin\\java.exe")) { hits.push_back({ dir + L"\\bin\\java.exe", source }); return; }

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        ScanDirTree(dir + L"\\" + fd.cFileName, depth - 1, source, hits);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

std::wstring EnvOf(const wchar_t* name) {
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    if (n == 0 || n >= 1024) return std::wstring();
    return Trim(std::wstring(buf, n));
}

// ---- 深度扫描用的小工具 ----

std::wstring NormDir(std::wstring p) {
    for (auto& c : p)
        if (c == L'/') c = L'\\';
    while (!p.empty() && p.back() == L'\\') p.pop_back();
    return p;
}

// path 等于 root 或在 root 之下（忽略大小写、按整段匹配，C:\Users 不会误伤 C:\Users2）
bool PathUnder(const std::wstring& path, const std::wstring& root) {
    if (root.empty()) return false;
    std::wstring a = Lower(path), b = Lower(root);
    if (a == b) return true;
    return a.size() > b.size() && a.compare(0, b.size(), b) == 0 && a[b.size()] == L'\\';
}

struct DeepCtx {
    const std::vector<std::wstring>* excludes = nullptr;
    std::vector<std::pair<std::wstring, std::wstring>>* hits = nullptr;
    const std::function<bool(const std::wstring&, int, int)>* progress = nullptr;
    int dirs = 0;
    bool stopped = false;

    bool Excluded(const std::wstring& d) const {
        for (const auto& e : *excludes)
            if (PathUnder(d, e)) return true;
        return false;
    }
};

void WalkDeep(const std::wstring& dir, int depth, DeepCtx& c) {
    if (c.stopped || depth < 0) return;
    if (c.Excluded(dir)) return;
    ++c.dirs;
    // 每 64 个目录报一次进度，别让回调本身成为开销
    if ((c.dirs & 63) == 0 && c.progress && !(*c.progress)(dir, c.dirs, (int)c.hits->size())) {
        c.stopped = true;
        return;
    }
    if (FileExists(dir + L"\\bin\\javaw.exe")) {
        c.hits->push_back({ dir + L"\\bin\\javaw.exe", L"深度扫描" });
        return;                        // 已经是一份运行时，别往下再钻
    }
    if (FileExists(dir + L"\\bin\\java.exe")) {
        c.hits->push_back({ dir + L"\\bin\\java.exe", L"深度扫描" });
        return;
    }

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;   // 链接不跟，免成环
        WalkDeep(dir + L"\\" + fd.cFileName, depth - 1, c);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

} // namespace

int JavaMajorOf(const std::wstring& version) {
    std::wstring v = Trim(version);
    if (v.empty()) return 0;
    if (v[0] == L'1' && v.size() > 2 && v[1] == L'.') v = v.substr(2);   // 1.8.0_442
    int major = 0;
    size_t i = 0;
    while (i < v.size() && iswdigit(v[i])) major = major * 10 + (v[i++] - L'0');
    return major;
}

bool JavaIsLts(int major, const std::wstring& version) {
    if (Lower(version).find(L"lts") != std::wstring::npos) return true;
    return major == 8 || major == 11 || major == 17 || major == 21 || major == 25;
}

int JavaVersionCompare(const JavaRuntime& a, const JavaRuntime& b) {
    if (a.major != b.major) return a.major - b.major;
    auto nums = [](const std::wstring& s) {
        std::vector<int> out;
        int cur = 0;
        bool has = false;
        for (wchar_t c : s) {
            if (iswdigit(c)) { cur = cur * 10 + (c - L'0'); has = true; }
            else { if (has) out.push_back(cur); cur = 0; has = false; }
        }
        if (has) out.push_back(cur);
        return out;
    };
    std::vector<int> x = nums(a.version), y = nums(b.version);
    for (size_t i = 0; i < x.size() && i < y.size(); ++i)
        if (x[i] != y[i]) return x[i] - y[i];
    return (int)x.size() - (int)y.size();
}

std::wstring JavaInstallDir() {
    return JoinPath(AppCacheDir(), L"java");
}

bool JavaInspect(const std::wstring& fileOrHome, JavaRuntime& out, const std::wstring& source) {
    std::wstring p = Unquote(Trim(fileOrHome));
    if (p.empty()) return false;

    std::wstring exe, home;
    if (EndsWithNoCase(p, L"javaw.exe") || EndsWithNoCase(p, L"java.exe")) {
        size_t cut = p.find_last_of(L"\\/");
        exe = p;
        size_t cut2 = cut == std::wstring::npos ? std::wstring::npos : p.find_last_of(L"\\/", cut - 1);
        home = (cut2 == std::wstring::npos) ? p.substr(0, cut) : p.substr(0, cut2);
    } else {
        home = p;
        if (FileExists(home + L"\\bin\\javaw.exe")) exe = home + L"\\bin\\javaw.exe";
        else if (FileExists(home + L"\\bin\\java.exe")) exe = home + L"\\bin\\java.exe";
        else if (FileExists(home + L"\\javaw.exe")) { exe = home + L"\\javaw.exe"; }
    }
    if (exe.empty()) return false;

    JavaRuntime rt;
    rt.home = home;
    rt.exe = exe;
    rt.source = source;

    std::map<std::wstring, std::wstring> kv;
    ReadRelease(home, kv);
    auto v = [&kv](const wchar_t* k) {
        auto it = kv.find(k);
        return it == kv.end() ? std::wstring() : Unquote(it->second);
    };
    rt.version = v(L"JAVA_VERSION");
    if (rt.version.empty()) rt.version = v(L"JAVA_RUNTIME_VERSION");
    rt.implementor = v(L"IMPLEMENTOR");
    rt.major = JavaMajorOf(rt.version);
    rt.vendor = VendorOf(rt.implementor, v(L"IMPLEMENTOR_VERSION") + L" " + v(L"JAVA_RUNTIME_NAME"));

    // 没 release 的老 JDK：从目录名里捡数字当版本
    if (rt.major == 0) {
        std::wstring dirName = home;
        size_t cut = dirName.find_last_of(L"\\/");
        if (cut != std::wstring::npos) dirName = dirName.substr(cut + 1);
        int m = 0;
        for (size_t i = 0; i + 2 < dirName.size(); ++i) {
            if ((dirName.compare(i, 3, L"jdk") == 0 || dirName.compare(i, 3, L"jre") == 0) ||
                (i + 3 <= dirName.size() && dirName.compare(i, 3, L"bin") == 0)) {
                m = JavaMajorOf(dirName.substr(i + 3));
                if (m > 0) break;
            }
        }
        if (m > 0) { rt.major = m; rt.version = std::to_wstring(m); }
    }

    // JDK / JRE：有编译器就是 JDK
    rt.jdk = FileExists(home + L"\\bin\\javac.exe") ||
             FileExists(home + L"\\bin\\jlink.exe") ||
             DirExists(home + L"\\jmods");

    // 位数：PE 头最准，其次 release 的 OS_ARCH
    bool isX64 = true;
    if (PeIsX64(exe, isX64)) {
        rt.x64 = isX64;
    } else {
        std::wstring arch = Lower(v(L"OS_ARCH"));
        rt.x64 = arch.empty() ? true : (arch.find(L"64") != std::wstring::npos || arch == L"amd64");
    }

    rt.lts = JavaIsLts(rt.major, rt.version);
    out = rt;
    return true;
}

std::vector<JavaRuntime> JavaScan(const std::vector<std::wstring>& extraPaths) {
    std::vector<std::pair<std::wstring, std::wstring>> hits;

    // 就扫这几处：PATH、JAVA_HOME 系列环境变量、启动器自己的 java 目录、手动加过的路径。
    // 不去翻 Program Files（那棵树太大，扫一次能把 UI 卡住）
    for (const wchar_t* env : { L"JAVA_HOME", L"JDK_HOME", L"JRE_HOME" }) {
        std::wstring v = EnvOf(env);
        if (!v.empty()) hits.push_back({ v, std::wstring(L"环境变量 ") + env });
    }

    // 带 _X64 后缀的 JAVA_HOME_21_X64 之类（部分安装器会写）
    if (LPWCH block = GetEnvironmentStringsW()) {
        const wchar_t* p = block;
        while (*p) {
            std::wstring item = p;
            p += item.size() + 1;
            if (item.empty()) continue;
            size_t eq = item.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring key = item.substr(0, eq);
            if (key.rfind(L"JAVA_HOME", 0) != 0 && key.rfind(L"JDK_HOME", 0) != 0) continue;
            std::wstring val = item.substr(eq + 1);
            if (!val.empty()) hits.push_back({ val, std::wstring(L"环境变量 ") + key });
        }
        FreeEnvironmentStringsW(block);
    }

    // PATH 里每一个目录
    std::wstring path = EnvOf(L"PATH");
    size_t start = 0;
    while (start < path.size()) {
        size_t sep = path.find(L';', start);
        std::wstring dir = Trim(path.substr(start, sep == std::wstring::npos ? std::wstring::npos : sep - start));
        start = sep == std::wstring::npos ? path.size() : sep + 1;
        if (dir.empty()) continue;
        if (FileExists(dir + L"\\javaw.exe")) hits.push_back({ dir + L"\\javaw.exe", L"PATH" });
        else if (FileExists(dir + L"\\java.exe")) hits.push_back({ dir + L"\\java.exe", L"PATH" });
    }

    // 启动器自己下载的
    ScanDirTree(JavaInstallDir(), 2, L"已下载", hits);

    // 配置里手工加过的
    for (const auto& p : extraPaths)
        if (!p.empty()) hits.push_back({ p, L"手动添加" });

    std::vector<JavaRuntime> out;
    std::map<std::wstring, size_t> seen;
    for (auto& h : hits) {
        JavaRuntime rt;
        if (!JavaInspect(h.first, rt, h.second)) continue;
        std::wstring key = Lower(rt.home);
        auto it = seen.find(key);
        if (it != seen.end()) {
            // 同一份 Java 被多处发现：来源更具体的优先（手动 > 已下载 > 注册表 > PATH）

            auto rank = [](const std::wstring& s) {
                if (s == L"手动添加") return 0;
                if (s == L"已下载") return 1;
                if (s == L"注册表") return 2;
                if (s.rfind(L"环境变量", 0) == 0) return 3;
                if (s == L"常见目录") return 4;
                return 5;
            };
            if (rank(rt.source) < rank(out[it->second].source)) out[it->second].source = rt.source;
            continue;
        }
        seen[key] = out.size();
        out.push_back(rt);
    }

    std::sort(out.begin(), out.end(), [](const JavaRuntime& a, const JavaRuntime& b) {
        if (a.major != b.major) return a.major > b.major;
        if (a.jdk != b.jdk) return a.jdk;      // 同版本 JDK 排在 JRE 前
        if (a.x64 != b.x64) return a.x64;
        return JavaVersionCompare(a, b) > 0;
    });
    return out;
}

std::vector<JavaRuntime> JavaDeepScan(
    const std::vector<std::wstring>& excludes,
    const std::function<bool(const std::wstring&, int, int)>& progress) {
    std::vector<std::wstring> ex;
    for (const auto& e : excludes) {
        std::wstring n = NormDir(Trim(e));
        if (!n.empty()) ex.push_back(n);
    }

    DeepCtx c;
    c.excludes = &ex;
    std::vector<std::pair<std::wstring, std::wstring>> hits;
    c.hits = &hits;
    c.progress = &progress;

    // 只扫固定磁盘：U 盘、光驱、网络盘不碰
    wchar_t drives[512]{};
    DWORD n = GetLogicalDriveStringsW(511, drives);
    for (DWORD i = 0; i < n;) {
        std::wstring root = NormDir(std::wstring(drives + i));
        i += (DWORD)wcslen(drives + i) + 1;
        if (root.empty()) continue;
        if (GetDriveTypeW((root + L"\\").c_str()) != DRIVE_FIXED) continue;
        WalkDeep(root, 8, c);          // 8 层：Program Files\Java\jdk-21\bin 这类都够
        if (c.stopped) break;
    }

    std::vector<JavaRuntime> out;
    for (auto& h : hits) {
        JavaRuntime rt;
        if (!JavaInspect(h.first, rt, L"深度扫描")) continue;
        bool dup = false;
        for (const auto& o : out)
            if (Lower(o.home) == Lower(rt.home)) { dup = true; break; }
        if (!dup) out.push_back(rt);
    }
    return out;
}
