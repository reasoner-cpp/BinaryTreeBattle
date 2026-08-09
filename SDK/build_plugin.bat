@echo off
rem ============================================================
rem  Binary Tree Battle — AI 插件一键编译脚本 (MinGW g++)
rem ------------------------------------------------------------
rem  用法：把整个 SDK 文件夹拷到你的电脑，双击本脚本。
rem  编译成功后会生成 sample_plugin.dll，
rem  把它复制到游戏目录的 ai_plugins\ 里，重启游戏即可在
rem  vs AI 菜单最下方看到 "◆ Sample Advance AI"。
rem
rem  需要 g++ 在 PATH 中（装 TDM-GCC 或 Dev-C++ 并勾选加入 PATH）。
rem  如果你用 MSVC，用命令：
rem     cl /LD /O2 /std:c++17 /DAI_PLUGIN_BUILD sample_plugin.cpp
rem ============================================================
setlocal

if not exist ai_plugin.h (
    echo [ERROR] ai_plugin.h not found in current folder.
    echo         Please run this script from inside the SDK folder.
    pause
    exit /b 1
)

echo === Compiling sample_plugin.cpp with MinGW g++ ===
g++ -O2 -shared -static -static-libgcc -static-libstdc++ -DAI_PLUGIN_BUILD -I. sample_plugin.cpp -o sample_plugin.dll

if errorlevel 1 (
    echo.
    echo [FAILED] Compile error. Make sure g++ is installed and in PATH.
) else (
    echo.
    echo [OK] sample_plugin.dll generated.
    echo Copy it into the game's ai_plugins\ folder, then restart the game.
)
echo.
pause
