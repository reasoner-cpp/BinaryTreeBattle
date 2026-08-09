cmake_minimum_required(VERSION 3.16)
project(BinaryTreeBattle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 CRT (/MT): 单文件免装 VC++ 运行库即可分发给玩家
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime library" FORCE)
endif()

# Win32 + GDI+ 版本 —— 仅依赖 Windows 系统自带库
add_executable(BinaryTreeBattle WIN32 src/main.cpp src/ai.cpp src/ai_plugin_host.cpp)

# 联机服务器: 运行即开启公网 IPv6 端口 (默认 8080)
add_executable(btbserver src/btbserver.cpp)

# 自我对弈强度评估器
add_executable(selfplay2 src/selfplay2.cpp src/ai.cpp)

# 示例 AI 插件 (编译为 DLL, 输出到 <构建目录>/ai_plugins/)
add_library(sample_ai SHARED src/sample_plugin.cpp)
target_compile_definitions(sample_ai PRIVATE AI_PLUGIN_BUILD)
set_target_properties(sample_ai PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ai_plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ai_plugins")

if(MSVC)
    target_compile_options(BinaryTreeBattle PRIVATE /utf-8)
    target_compile_options(selfplay2 PRIVATE /utf-8)
    target_compile_options(sample_ai PRIVATE /utf-8)
    target_compile_options(btbserver PRIVATE /utf-8)
endif()

# GDI+ (Windows 自带)
target_link_libraries(BinaryTreeBattle PRIVATE gdiplus)
target_link_libraries(selfplay2 PRIVATE gdiplus)
target_link_libraries(btbserver PRIVATE ws2_32)
