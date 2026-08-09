// ===== 游戏侧插件加载器实现 =====
#include "ai_plugin_host.h"

static std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return L".";
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p = p.substr(0, pos);
    return p;
}

std::vector<AIPlugin> aiPluginLoadAll() {
    std::vector<AIPlugin> out;
    std::wstring dir = exeDir() + L"\\ai_plugins";
    std::wstring pat = dir + L"\\*.dll";

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;   // 目录不存在或空

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        HMODULE m = LoadLibraryW(full.c_str());
        if (!m) continue;

        AIPlugin pl;
        pl.mod = m;
        pl.path = full;
        pl.fnName        = (const char* (*)())GetProcAddress(m, "aiPluginName");
        pl.fnAuthor      = (const char* (*)())GetProcAddress(m, "aiPluginAuthor");
        pl.fnApiVersion  = (int (*)())GetProcAddress(m, "aiPluginApiVersion");
        pl.fnGetMove     = (int (*)(const AIPluginState*, AIPluginMove*))GetProcAddress(m, "aiPluginGetMove");
        pl.fnGetCands    = (int (*)(const AIPluginState*, AIPluginCand*, int))GetProcAddress(m, "aiPluginGetCands");
        pl.fnInit        = (void (*)())GetProcAddress(m, "aiPluginInit");
        pl.fnShutdown    = (void (*)())GetProcAddress(m, "aiPluginShutdown");
        pl.fnThinkStart  = (void (*)(const AIPluginState*))GetProcAddress(m, "aiPluginThinkStart");

        // 必需导出：名称 / API版本 / getMove；版本不匹配则跳过
        if (!pl.fnName || !pl.fnGetMove || !pl.fnApiVersion || pl.fnApiVersion() != AI_PLUGIN_API_VERSION) {
            FreeLibrary(m);
            continue;
        }
        pl.name = pl.fnName() ? pl.fnName() : "Unknown";
        pl.author = (pl.fnAuthor && pl.fnAuthor()) ? pl.fnAuthor() : "";
        pl.loaded = true;
        if (pl.fnInit) pl.fnInit();
        out.push_back(std::move(pl));
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return out;
}

void aiPluginUnloadAll(std::vector<AIPlugin>& plugins) {
    for (auto& pl : plugins) {
        if (pl.fnShutdown) pl.fnShutdown();
        if (pl.mod) { FreeLibrary(pl.mod); pl.mod = nullptr; }
        pl.loaded = false;
    }
    plugins.clear();
}
