cmake_minimum_required(VERSION 3.16)
project(BinaryTreeBattle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 CRT (/MT): 单文件免装 VC++ 运行库即可分发给玩家
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime library" FORCE)
endif()

# Win32 + GDI+ 版本 —— 仅依赖 Windows 系统自带库
add_executable(BinaryTreeBattle WIN32 src/main.cpp src/ai.cpp)

# 自我对弈强度评估器
add_executable(selfplay2 src/selfplay2.cpp src/ai.cpp)

if(MSVC)
    target_compile_options(BinaryTreeBattle PRIVATE /utf-8)
    target_compile_options(selfplay2 PRIVATE /utf-8)
endif()

# Windows 系统自带库 (仅依赖系统自带, 无需第三方)
# 显式列出便于 MinGW 链接; MSVC 下与源码中的 #pragma comment(lib) 等价, 二者共存无害
target_link_libraries(BinaryTreeBattle PRIVATE gdiplus winmm)
target_link_libraries(selfplay2 PRIVATE gdiplus)
