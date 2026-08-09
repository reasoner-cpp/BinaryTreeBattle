#pragma once
// ===== 游戏侧插件加载器 =====
#include <string>
#include <vector>
#include <windows.h>
#include "ai_plugin.h"

// 一个已加载的插件实例
struct AIPlugin {
    HMODULE mod = nullptr;
    std::wstring path;
    std::string name;    // aiPluginName()
    std::string author;  // aiPluginAuthor()
    bool loaded = false;

    // 解析出的导出函数
    const char* (*fnName)() = nullptr;
    const char* (*fnAuthor)() = nullptr;
    int  (*fnApiVersion)() = nullptr;
    int  (*fnGetMove)(const AIPluginState*, AIPluginMove*) = nullptr;
    int  (*fnGetCands)(const AIPluginState*, AIPluginCand*, int) = nullptr;
    void (*fnInit)() = nullptr;
    void (*fnShutdown)() = nullptr;
    void (*fnThinkStart)(const AIPluginState*) = nullptr;
};

// 扫描 <exe目录>\ai_plugins\*.dll，加载所有合法插件（调用 aiPluginInit）
std::vector<AIPlugin> aiPluginLoadAll();

// 卸载全部插件（调用 aiPluginShutdown + FreeLibrary）
void aiPluginUnloadAll(std::vector<AIPlugin>& plugins);
