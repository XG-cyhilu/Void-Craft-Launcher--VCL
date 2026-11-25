#pragma once
#include <string>

// resource 目录在构建时被打进 exe（生成 .rc，走 RT_RCDATA），运行时从模块里取。
// 这样发布就是一个单文件，不用把 resource 目录跟着 exe 一起搬。
namespace vcl {

// 相对 resource 目录的路径，如 L"image/Grass_Block.png"、L"modwiki/ModWiki.json"
std::wstring EmbeddedName(const std::wstring& rel);

// 取打包进 exe 的条目；data 指向模块内存（进程生命周期内有效），size 是字节数
bool EmbeddedResource(const std::wstring& rel, const void** data, unsigned* size);

} // namespace vcl
