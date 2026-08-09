/**
 * 二叉树对战模拟器 (Binary Tree Battle)
 * Win32 + GDI+ 重构版 —— 仅使用 Windows 系统自带库
 *
 * 特性:
 *   - 得分点 (1/2分) → 触碰收集; 每回合生成1个
 *   - 积分: 初始10分, 每3分换1次额外行动
 *   - 边强度: 默认1, 1分+1级; 敌人穿越-1, 归零消失
 *   - 距离扩展: 空格0~3级 (+40/级, 每级1分)
 *   - Ctrl+Z 回退快照 / Ctrl+R 重开 / X 额外行动
 *   - AI 对战 (红方) / PvP 双模式
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
// 目标 Windows 版本: Vista+ (需要 GetTickCount64 / SetProcessDPIAware 等)
// 在包含 windows.h 之前定义; MSVC 默认已满足, MinGW 需要显式指定
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>   // 必须先于 windows.h (联机对战)
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <windows.h>
#include <objidl.h>     // IStream (GDI+ 需要)
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ai_plugin.h"
#include "ai_plugin_host.h"
#include <cmath>
#include <array>
#include <vector>
#include <memory>
#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <map>
#include <functional>
#include <thread>
#include <ctime>
#include <shellapi.h>
#pragma comment(lib,"shell32.lib")
#include <mmsystem.h>
#include <commdlg.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")

#include "ai.h"

using namespace Gdiplus;

// ===== 安全整数: 内存加密 + 合法性校验 (防修改器篡改) =====
class SecureInt {
    volatile int m_enc;   // 加密后的值
    int m_key;            // 随机掩码
    int m_min;            // 合法最小值
    int m_max;            // 合法最大值
public:
    SecureInt(int v = 0, int minV = 0, int maxV = 1000)
        : m_min(minV), m_max(maxV) {
        static int s = 123456789;
        s = s * 1103515245 + 12345;
        m_key = (s ^ (int)(time(nullptr) ^ 0x9E3779B9)) | 1;  // 随机且非零
        m_enc = (v < m_min ? m_min : (v > m_max ? m_max : v)) ^ m_key;
    }
    operator int() const {
        int v = m_enc ^ m_key;
        // 合法性校验: 值超出合法范围 → 判定被篡改, 重置为下限
        if (v < m_min || v > m_max) {
            const_cast<SecureInt*>(this)->m_enc = m_min ^ m_key;
            return m_min;
        }
        return v;
    }
    SecureInt& operator=(int v) {
        if (v < m_min) v = m_min; else if (v > m_max) v = m_max;
        m_enc = v ^ m_key;
        return *this;
    }
    SecureInt& operator+=(int v) {
        int d = int(*this);                 // 带校验读取
        int r = d + v;
        if (r < m_min) r = m_min; else if (r > m_max) r = m_max;
        m_enc = r ^ m_key;
        return *this;
    }
    SecureInt& operator-=(int v) {
        int d = int(*this);
        int r = d - v;
        if (r < m_min) r = m_min; else if (r > m_max) r = m_max;
        m_enc = r ^ m_key;
        return *this;
    }
    SecureInt& operator++() { return (*this) += 1; }
    SecureInt& operator--() { return (*this) -= 1; }
    bool operator==(int v) const { return int(*this) == v; }
    bool operator!=(int v) const { return int(*this) != v; }
    bool operator<(int v) const { return int(*this) < v; }
    bool operator>(int v) const { return int(*this) > v; }
    bool operator<=(int v) const { return int(*this) <= v; }
    bool operator>=(int v) const { return int(*this) >= v; }
    bool operator==(const SecureInt& o) const { return int(*this) == int(o); }
    bool operator!=(const SecureInt& o) const { return int(*this) != int(o); }
    bool operator<(const SecureInt& o) const { return int(*this) < int(o); }
    bool operator<=(const SecureInt& o) const { return int(*this) <= int(o); }
    bool operator>(const SecureInt& o) const { return int(*this) > int(o); }
    bool operator>=(const SecureInt& o) const { return int(*this) >= int(o); }
};

// ===== 常量 =====
// 地图尺寸 WIN_W/WIN_H/FULL_W/PANEL_W 定义在 ai.h (设置页可调, 节点大小不变)
constexpr float NODE_R = 10, MAX_D = 120, EXTRA_D = 40;
constexpr float SP_R = 8, COLLECT_R = 18, OCCUPY_R = 30, ATK_M = 2;
constexpr int MAX_SP = 5, INIT_SP = 3, DEF_S = 1, MAX_S = 5;
constexpr int START_SCORE = 10, EXTRA_COST = 3;
constexpr float SNAP_EDGE = 12.f;   // 边界吸附距离

// 颜色 (A,R,G,B)
const Color CLR_RED   {255,220, 53, 69};
const Color CLR_BLUE  {255,  0,123,255};
const Color CLR_GREEN {255, 46,204,113};
const Color CLR_YELLOW{255,230,180, 40};
const Color CLR_GOLD  {255,255,215,  0};
const Color CLR_DGOLD {255,218,165, 32};
const Color CLR_BG    {255,243,244,247};
const Color CLR_WRED  {255,255,150,150};
// V6.2.0: 4 阵营颜色 (红/绿/蓝/黄), 前 2 个用于 2 人模式
const Color CLR_TEAMS[4] = {CLR_RED, CLR_BLUE, CLR_GREEN, CLR_YELLOW};

// V6.2.0: 根/分数组化, 用宏保持旧代码 m_rRoot/m_bRoot 等兼容 (2人=索引0/1, 4人=0..3)
// 注意: m_scores 是得分点(vector), 玩家分数数组用 m_plyScores
#define m_rRoot m_roots[0]
#define m_bRoot m_roots[1]
#define m_rScore m_plyScores[0]
#define m_bScore m_plyScores[1]
const Color CLR_WBLUE {255,150,180,255};

enum class State { Menu, Playing, GameOver, Replay };

// 对局行动记录 (btbdt 格式)
struct ReplayAction {
    int turn = 0;         // 回合数
    int team = 0;         // 0=红 1=蓝
    int type = 0;         // 0=创建分支 1=强化 2=额外行动
    float px = 0, py = 0; // 父节点位置
    float tx = 0, ty = 0; // 目标位置
    int strength = 1;     // 分支强度
    int extend = 0;       // 扩展级数
    int scBefore = 0;     // 行动前积分
    int scAfter = 0;      // 行动后积分
    int nodesKilled = 0;  // 命中敌方节点数
    int edgesKilled = 0;  // 摧毁敌方边数
};

// 回放用得分黄点记录 (btbdt [S] 段: 开局黄点布局)
struct ReplayScorePt {
    float x = 0, y = 0;   // 位置
    int v = 1;            // 价值 1~2
};

// 回放黄点实时统计 (V6.2.0: 支持 4 方)
struct ReplayScoreStats {
    int total = 0;        // 开局黄点总数
    int got[4] = {0,0,0,0}, val[4] = {0,0,0,0};   // 各方收集数/分值
    int remain = 0;       // 当前剩余
    int ghostAdded = 0;   // 旧文件无黄点数据时推断出的黄点数
};

// 攻击预览结果
struct AttackPreview {
    int nodesHit = 0;      // 直接命中的敌方节点数
    int edgesKilled = 0;   // 会被摧毁的边数 (强度1)
    int edgesWeakened = 0; // 会被削弱的边数 (强度>1)
    bool hitRoot = false;  // 是否命中敌根
    std::vector<Node*> hitNodes;     // 命中的敌方节点
    std::vector<Node*> edgesToKill;  // 将被摧毁的边 (子节点)
};

// Color 比较辅助 (GDI+ Color 无 operator==)
static bool teamEq(const Color& a,const Color& b){ return a.GetValue()==b.GetValue(); }

// ===== 几何工具 =====
static float len2(PointF a, PointF b){ float dx=a.X-b.X,dy=a.Y-b.Y; return std::sqrt(dx*dx+dy*dy); }
static bool ccw(PointF a,PointF b,PointF c){ return (c.Y-a.Y)*(b.X-a.X)>(b.Y-a.Y)*(c.X-a.X); }
static bool ptEq(PointF a,PointF b){ return a.X==b.X&&a.Y==b.Y; }
static bool segCross(PointF p1,PointF p2,PointF p3,PointF p4){
    if(ptEq(p1,p3)||ptEq(p1,p4)||ptEq(p2,p3)||ptEq(p2,p4))return false;
    return ccw(p1,p3,p4)!=ccw(p2,p3,p4)&&ccw(p1,p2,p3)!=ccw(p1,p2,p4);
}
static float ptSegDist(PointF p,PointF a,PointF b){
    float dx=b.X-a.X,dy=b.Y-a.Y;
    if(dx==0&&dy==0)return len2(p,a);
    float t=std::max(0.f,std::min(1.f,((p.X-a.X)*dx+(p.Y-a.Y)*dy)/(dx*dx+dy*dy)));
    return len2(p,{a.X+t*dx,a.Y+t*dy});
}
static PointF toPt(POINT p){ return {(float)p.x,(float)p.y}; }

// ===== 简易文本输入对话框 (联机服务器地址输入) =====
static std::wstring g_promptResult;
static std::wstring g_promptInit;
static bool g_promptDone=false;
static LRESULT CALLBACK PromptWndProc(HWND h, UINT m, WPARAM w, LPARAM l){
    switch(m){
    case WM_COMMAND:
        if(LOWORD(w)==1){   // 确定
            wchar_t buf[512]; GetDlgItemTextW(h,100,buf,512);
            g_promptResult=buf; g_promptDone=true;
            DestroyWindow(h); return 0;
        }
        if(LOWORD(w)==2){   // 取消
            g_promptResult.clear(); g_promptDone=true;
            DestroyWindow(h); return 0;
        }
        break;
    case WM_CLOSE:
        g_promptResult.clear(); g_promptDone=true;
        DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
static std::string promptInput(const wchar_t* title, const wchar_t* defVal){
    g_promptResult.clear(); g_promptDone=false; g_promptInit=defVal;
    WNDCLASSEXW wc{};
    wc.cbSize=sizeof wc;
    wc.lpfnWndProc=PromptWndProc;
    wc.hInstance=GetModuleHandleW(nullptr);
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.lpszClassName=L"BTBPrompt";
    RegisterClassExW(&wc);
    HWND hw=CreateWindowExW(WS_EX_TOPMOST,L"BTBPrompt",title,
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        CW_USEDEFAULT,CW_USEDEFAULT,460,150,nullptr,nullptr,wc.hInstance,nullptr);
    CreateWindowExW(0,L"STATIC",title,WS_CHILD|WS_VISIBLE,20,15,420,20,hw,nullptr,wc.hInstance,nullptr);
    HWND hEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",g_promptInit.c_str(),
        WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_WANTRETURN,
        20,45,420,28,hw,(HMENU)100,wc.hInstance,nullptr);
    CreateWindowExW(0,L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
        180,90,80,28,hw,(HMENU)1,wc.hInstance,nullptr);
    CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE,
        270,90,80,28,hw,(HMENU)2,wc.hInstance,nullptr);
    ShowWindow(hw,SW_SHOW);
    SetFocus(hEdit);   // 关键: 输入框获得焦点, 否则键盘输入无效
    // 标准模态循环 (与系统对话框一致): GetMessageW + IsDialogMessage
    // 主窗口由调用方 EnableWindow 禁用; WM_QUIT (用户关闭主窗口) 显式处理退出
    MSG msg{};
    while(!g_promptDone && GetMessageW(&msg,nullptr,0,0)>0){
        if(msg.message==WM_QUIT){ g_promptDone=true; g_promptResult.clear(); break; }
        if(!IsDialogMessageW(hw,&msg)){
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
    }
    std::string out;
    int n=WideCharToMultiByte(CP_UTF8,0,g_promptResult.c_str(),(int)g_promptResult.size(),nullptr,0,nullptr,nullptr);
    if(n>0){ out.resize(n); WideCharToMultiByte(CP_UTF8,0,g_promptResult.c_str(),(int)g_promptResult.size(),&out[0],n,nullptr,nullptr); }
    return out;
}

// HSV → RGB (h:0-360, s/v:0-1) 用于循环彩色
static Color hsvColor(float h, float s, float v){
    float c=v*s;
    float x=c*(1.f-fabs(fmod(h/60.f,2.f)-1.f));
    float m=v-c;
    float r=0.f,g=0.f,b=0.f;
    if(h<60){r=c;g=x;} else if(h<120){r=x;g=c;}
    else if(h<180){g=c;b=x;} else if(h<240){g=x;b=c;}
    else if(h<300){r=x;b=c;} else{r=c;b=x;}
    return Color(255,(BYTE)((r+m)*255.f),(BYTE)((g+m)*255.f),(BYTE)((b+m)*255.f));
}

// ===== 游戏类 =====
class Game {
public:
    static Game& I(){ static Game g; return g; }

    bool init(HINSTANCE hInst){
        m_hInst=hInst;
        // 启动 Winsock (联机对战)
        WSADATA wsa;
        WSAStartup(MAKEWORD(2,2), &wsa);
        // 启动 GDI+
        GdiplusStartupInput gsi;
        if(GdiplusStartup(&m_gdiToken,&gsi,nullptr)!=Ok) return false;
        // DPI 感知 (保证坐标正确)
        SetProcessDPIAware();
        timeBeginPeriod(1);   // 1ms 定时器精度 → 动画平滑
        initFont();
        loadSettings();       // 读取 settings.dat 并应用地图尺寸 (窗口创建前)
        WNDCLASSEXW wc{};
        wc.cbSize=sizeof wc;
        wc.style=CS_HREDRAW|CS_VREDRAW;
        wc.lpfnWndProc=Game::WndProc;
        wc.hInstance=hInst;
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
        wc.lpszClassName=L"BTreeBattle";
        if(!RegisterClassExW(&wc)) return false;
        RECT rc{0,0,FULL_W,WIN_H};
        AdjustWindowRectEx(&rc,WS_OVERLAPPEDWINDOW,FALSE,0);
        m_hwnd=CreateWindowExW(0,L"BTreeBattle",L"Binary Tree Battle V6.3.0",
            WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,
            rc.right-rc.left,rc.bottom-rc.top,nullptr,nullptr,hInst,nullptr);
        if(!m_hwnd) return false;
        m_rng.seed((unsigned)time(nullptr));
        // ===== AI Reasoner 配置 (ai_reasoner_XXXX.dat) =====
        // 扫描所有 reasoner 文件; 没有则首次扫描 .btb 学习并创建 0001
        scanReasoners();
        // ===== 加载 AI 插件 (ai_plugins\*.dll) =====
        m_plugins = aiPluginLoadAll();
        return true;
    }

    int run(){
        MSG msg{};
        ULONGLONG last=GetTickCount64();
        while(msg.message!=WM_QUIT){
            if(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }else{
                ULONGLONG now=GetTickCount64();
                frame((float)(now-last)/1000.f);
                last=now;
                // 仅在需要动画时重绘 (避免无谓的 60fps 全图 GDI+ 重绘):
                // 菜单脉动/拖拽跟随/AI思考热力图/回放播放/AIvsAI
                bool needPaint = (m_state==State::Menu)
                              || (m_state==State::Playing && (m_aiThinking || m_sel || m_bothAI || m_pluginCandsValid))
                              || (m_state==State::Replay)
                              || (m_state==State::GameOver);
                if(needPaint) InvalidateRect(m_hwnd,nullptr,FALSE);
                // 60fps 帧率限制 → 动画平滑稳定
                ULONGLONG spent=GetTickCount64()-now;
                if(spent<16) Sleep((DWORD)(16-spent));
            }
        }
        GdiplusShutdown(m_gdiToken);
        return (int)msg.wParam;
    }

private:
    // ===== 消息处理 =====
    static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
        Game& g=I();
        switch(m){
        case WM_MOUSEMOVE:
        {
            PointF np=toPt({(short)LOWORD(l),(short)HIWORD(l)});
            float dx=np.X-g.m_mouse.X, dy=np.Y-g.m_mouse.Y;
            g.m_mouse=np;
            // 拖拽中: 每帧跟随; 悬停: 4px 节流 (减少全图重绘)
            if(g.m_sel) InvalidateRect(h,nullptr,FALSE);
            else if(dx*dx+dy*dy>16.f) InvalidateRect(h,nullptr,FALSE);
            return 0;
        }
        case WM_LBUTTONDOWN: g.onPress(g.m_mouse); InvalidateRect(h,nullptr,FALSE); return 0;
        case WM_LBUTTONUP: g.onRelease(g.m_mouse); InvalidateRect(h,nullptr,FALSE); return 0;
        case WM_RBUTTONDOWN: g.onRPress(g.m_mouse); InvalidateRect(h,nullptr,FALSE); return 0;
        case WM_MOUSEWHEEL: g.onWheel((float)GET_WHEEL_DELTA_WPARAM(w)/120.f); InvalidateRect(h,nullptr,FALSE); return 0;
        case WM_KEYDOWN: g.onKey((UINT)w); InvalidateRect(h,nullptr,FALSE); return 0;
        case WM_PAINT: g.paint(); return 0;
        case WM_ERASEBKGND: return 1; // 防闪烁
        case WM_DESTROY:
            g.saveSettings();          // 保存设置 (地图尺寸/快捷键开关)
            g.saveReasonersOnExit();   // 退出时回写本局用到的 AI Reasoner
            aiPluginUnloadAll(g.m_plugins);
            g.netDisconnect();
            WSACleanup();
            PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(h,m,w,l);
    }

    void frame(float dt){
        (void)dt;
        // 联机: 每帧轮询网络消息 (配对/对方动作/回合切换)
        if(m_state==State::Playing) netPoll();
        // AI 回合: 迭代思考驱动 (思考中实时展示选点动画)
        if(m_state==State::Playing&&!m_over){
            bool act=false;
            if(m_aiThinking) act=true;                                   // 思考中必须持续驱动
            else if(m_bothAI&&!m_didBranch) act=true;
            else if(m_aiMode&&teamEq(m_turn,CLR_RED)&&!m_didBranch) act=true;
            if(act){
                if(!m_aiThinking){
                    // 当前回合方是否自动化（AI 或插件）
                    bool autoSide = m_bothAI || (m_aiMode && teamEq(m_turn,CLR_RED));
                    if(autoSide){
                        int pidx = teamEq(m_turn,CLR_RED) ? m_redPlugin : m_bluePlugin;
                        if(pidx>=0 && pidx<(int)m_plugins.size() && m_plugins[pidx].loaded){
                            // 插件 AI: 每回合先等一小段(显示候选热力图), 再执行
                            int wt = teamEq(m_turn,CLR_RED) ? 0 : 1;
                            ULONGLONG now=GetTickCount64();
                            if(m_pluginWaitTeam != wt){
                                m_pluginWaitTeam = wt;
                                m_pluginTurnStart = now;
                                m_pluginCandsValid = false;   // 新回合需重新拉取
                            }
                            if(!m_pluginCandsValid){
                                populatePluginCands(m_turn);
                                m_pluginCandsValid = true;
                            }
                            if(now - m_pluginTurnStart >= PLUGIN_TURN_MS){
                                m_pluginWaitTeam = -1;
                                m_pluginCandsValid = false;
                                m_pluginCands.clear();
                                startPluginThink(m_turn);
                            }
                        } else {
                            m_diff = teamEq(m_turn,CLR_RED) ? m_redDiff : m_blueDiff;
                            if(teamEq(m_turn,CLR_RED)) startThink(CLR_RED,m_aiRed);
                            else startThink(CLR_BLUE,m_aiBlue); // 内置 AI
                        }
                    }
                } else {
                    // 每秒5帧驱动思考 (每200ms推进一批), 整个思考期持续探索
                    Color team=m_thinkingTeam;
                    AI& ai=*m_thinkingAI;
                    ULONGLONG now=GetTickCount64();
                    if(now-m_lastThinkTick>=200){
                        m_lastThinkTick=now;
                        bool done = ai.thinking() ? ai.thinkStep(m_all,myRootOf(team),enRootOf(team),
                                                                 scoreOf(team),enScoreOf(team),m_scores,team) : true;
                        ULONGLONG el=now-m_thinkStart;
                        // 一轮扫完但时间未到 → 开启新一轮扫描 (持续利用思考时间)
                        if(done && el<m_minThink && el<m_maxThink){
                            ai.rethink();
                        }
                        if((done&&el>=m_minThink)||el>=m_maxThink){
                            finishThink();
                        }
                    }
                }
                // 每帧刷新选点动画
                if(m_aiThinking) InvalidateRect(m_hwnd,nullptr,FALSE);
            }
        }
        // 回放播放驱动
        if(m_state==State::Replay){
            if(!m_repPaused){
                ULONGLONG now=GetTickCount64();
                float secs=(float)(now-m_repLastTick)/1000.f;
                if(secs>=m_repSpeed){
                    int steps=(int)(secs/m_repSpeed);
                    for(int i=0;i<steps && m_repIdx<(int)m_repActs.size();++i) replayStep();
                    m_repLastTick=now;
                }
            }
            // 进度条拖拽跟随
            if(m_repDrag){
                float bx=20.f, by=WIN_H-34.f, bw=WIN_W-40.f;
                if(m_mouse.X>=bx && m_mouse.X<=bx+bw && m_mouse.Y>=by-12.f && m_mouse.Y<=by+26.f){
                    float pct=std::max(0.f,std::min(1.f,(m_mouse.X-bx)/bw));
                    replayJumpTo((int)(pct*m_repActs.size()));
                }
            }
        }
        // 清理已收集得分点
        m_scores.erase(std::remove_if(m_scores.begin(),m_scores.end(),
            [](auto&s){return !s.alive;}),m_scores.end());
    }

    // ===== 游戏初始化 =====
    void initGame(){
        m_all.clear(); m_scores.clear(); m_history.clear();
        m_plyScores[0]=SecureInt(); m_plyScores[1]=SecureInt(); m_plyScores[2]=SecureInt(); m_plyScores[3]=SecureInt();
        // V6.2.0: 若 AI 参与则固定 2 人 (vs AI / AI Battle)
        if(m_aiMode || m_bothAI || m_netActive) m_players = 2;
        for(int i=0;i<4;++i) m_roots[i].reset();
        m_roots[0]=std::make_unique<Node>(Node{{80,80},CLR_TEAMS[0]});
        m_roots[1]=std::make_unique<Node>(Node{{WIN_W-80.f,WIN_H-80.f},CLR_TEAMS[1]});
        if(m_players>=3) m_roots[2]=std::make_unique<Node>(Node{{WIN_W-80.f,80.f},CLR_TEAMS[2]});
        if(m_players>=4) m_roots[3]=std::make_unique<Node>(Node{{80.f,WIN_H-80.f},CLR_TEAMS[3]});
        for(int i=0;i<m_players;++i) m_all.push_back(m_roots[i].get());
        m_turn=CLR_TEAMS[0]; m_sel=m_hover=nullptr; m_extend=0; m_str=DEF_S;
        m_reinf=nullptr; m_reinfStr=0; m_nodeMenu=nullptr; m_didBranch=false; m_xUsedThisTurn=false;
        m_pluginWaitTeam=-1; m_pluginTurnStart=0; m_pluginCandsValid=false; m_pluginCands.clear();
        // 困难难度: 内置 AI(红方)初始 15 分, 其余 10 分
        bool redAuto = m_aiMode || m_bothAI;
        for(int i=0;i<m_players;++i)
            m_plyScores[i] = (i==0 && redAuto && m_redPlugin==-1 && m_redDiff==2) ? 15 : START_SCORE;
        m_over=false; m_winner.clear();
        m_replay.clear(); m_replayTurn=0; m_replaySaved=false;
        m_aiThinking=false; m_thinkingAI=nullptr; m_hoverEdge=nullptr;
        m_state=State::Playing; m_enterClock.Restart(); m_aiClock.Restart();
        for(int i=0;i<m_settings.initScorePts;++i) spawnScore();
        // 开局黄点快照 (用于 btbdt 记录, 回放时精确统计)
        m_initScores = m_scores;
    }

    // ===== 得分点 =====
    void spawnScore(){
        int aliveCount=0;
        for(auto& s:m_scores) if(s.alive) aliveCount++;
        if(aliveCount>=m_settings.maxScorePts)return;   // 按存活数量判断 (避免已收集未清理的点占位)
        // 得分点全图随机生成 (公平: 不偏向任何一方)
        float xLo=40, xHi=WIN_W-40;
        std::uniform_real_distribution<float> xd(xLo,xHi),yd(40,WIN_H-40);
        int vv;
    { int rr = m_rng() % 100; if (rr < 40) vv = 1; else if (rr < 70) vv = 2; else vv = 3; }
        for(int t=0;t<50;++t){
            PointF p{xd(m_rng),yd(m_rng)};
            bool clash=false;
            for(auto&s:m_scores)if(s.alive&&len2(p,s.pos)<SP_R*4){clash=true;break;}
            if(clash)continue;
            for(auto*n:m_all)if(len2(p,n->pos)<NODE_R*3){clash=true;break;}
            if(!clash){m_scores.push_back({p,vv});return;}
        }
    }
    void collectAt(PointF p){
        SecureInt& sc=scoreOf(m_turn);
        bool got=false;
        for(auto&sp:m_scores)if(sp.alive&&len2(p,sp.pos)<COLLECT_R){
            sp.alive=false; sc+=sp.value; got=true;
        }
        // 收集后补生 (场上数量动态平衡, 不吃黄点不得分)
        if(got) spawnScore();
    }

    // ===== 节点管理 (无孤立机制: 摧毁即整棵子树销毁) =====
    void subtreeNodes(Node* r,std::vector<Node*>& out){
        out.push_back(r);
        for(auto&c:r->children)subtreeNodes(c.get(),out);
    }
    // 摧毁节点及其整棵子树: 先清空 m_all 中所有子孙指针, 再释放 (防悬空)
    void killSubtree(Node* node){
        if(!node)return;
        if(std::find(m_all.begin(),m_all.end(),node)==m_all.end())return; // 防悬空
        if(m_sel==node)m_sel=nullptr;
        if(m_hover==node)m_hover=nullptr;
        if(m_reinf==node){m_reinf=nullptr;m_reinfStr=0;}
        std::vector<Node*> dead; subtreeNodes(node, dead);
        for(Node* n : dead)
            m_all.erase(std::remove(m_all.begin(),m_all.end(),n),m_all.end());
        if(node->parent){
            auto&sib=node->parent->children;
            sib.erase(std::remove_if(sib.begin(),sib.end(),
                [node](auto&p){return p.get()==node;}),sib.end());
            node->parent=nullptr;
        }else{
            // V6.2.0: 遍历所有阵营根 (2人只用0/1, 4人含2/3)
            for(int i=0;i<4;++i)
                if(node==m_roots[i].get()) m_roots[i].reset();
        }
        m_graveyard.clear();   // 无孤立机制, 墓地恒为空
    }
    // 玩家删除节点: 返还子树边强化消耗一半, 整棵子树销毁
    void deleteNode(Node* n){
        if(!n||n->isolated)return;
        if(!n->parent)return;   // 根不可删
        SecureInt& sc=scoreOf(n->team);
        int refund=0;
        std::function<void(Node*)> sum=[&](Node* r){
            for(auto&c:r->children){refund+=c->edgeStrength-1; sum(c.get());}   // 按边强度返还
        };
        sum(n);
        refund/=2;
        int scBefore=(int)sc;
        sc+=refund;
        recordAction(3,n,n->pos,0,0,scBefore,(int)sc,n->team);
        killSubtree(n);   // 整棵子树销毁
    }
    void processAttack(PointF p1,PointF p2){
        std::set<Node*> hitNodes, crossEdges;
        for(auto*n:m_all){
            if(teamEq(n->team,m_turn))continue;
            if(ptEq(n->pos,p1)||ptEq(n->pos,p2))continue;
            if(ptSegDist(n->pos,p1,p2)<NODE_R+ATK_M) hitNodes.insert(n);
        }
        for(auto*n:m_all){
            if(teamEq(n->team,m_turn))continue;
            for(auto&c:n->children)
                if(c&&segCross(p1,p2,n->pos,c->pos)) crossEdges.insert(c.get());
        }
        // 命中节点本体: 削弱该节点连接父边的强度, 归零 → 整棵子树摧毁 (无节点等级)
        for(Node*t:hitNodes){
            if(std::find(m_all.begin(),m_all.end(),t)==m_all.end())continue; // 已被前序击杀销毁
            if(t->removed)continue;
            if(!t->parent){ killSubtree(t); continue; }   // 命中根 → 直接获胜
            if(--t->edgeStrength<=0) killSubtree(t);
        }
        // 根已被摧毁 → 游戏结束, 子树已销毁, 不再处理边 (防悬空)
        if(!m_rRoot || !m_bRoot) return;
        // 穿越边: 削弱线段强度, 归零 → 整棵子树摧毁
        for(Node*c:crossEdges){
            if(std::find(m_all.begin(),m_all.end(),c)==m_all.end())continue;
            if(--c->edgeStrength<=0) killSubtree(c);
        }
    }

    // 攻击预览 (只检测, 不实际攻击): 新分支 p1→p2 能切断哪些敌方目标
    AttackPreview previewAttack(PointF p1,PointF p2,const Color& attacker){
        AttackPreview r;
        for(auto*n:m_all){
            if(teamEq(n->team,attacker))continue;
            if(ptEq(n->pos,p1)||ptEq(n->pos,p2))continue;
            if(ptSegDist(n->pos,p1,p2)<NODE_R+ATK_M){
                r.nodesHit++;
                r.hitNodes.push_back(n);
                if(n->parent==nullptr) r.hitRoot=true;
            }
        }
        for(auto*n:m_all){
            if(teamEq(n->team,attacker))continue;
            for(auto&c:n->children)
                if(c&&segCross(p1,p2,n->pos,c->pos)){
                    if(c->edgeStrength<=1){ r.edgesKilled++; r.edgesToKill.push_back(c.get()); }
                    else r.edgesWeakened++;
                }
        }
        return r;
    }

    bool rootAlive(Node*r)const{
        if(!r)return false;
        return std::find(m_all.begin(),m_all.end(),r)!=m_all.end();
    }

    // ===== 对局记录 =====
    void recordAction(int type, Node* parent, PointF tgt, int str, int ext,
                      int scBefore, int scAfter, const Color& team){
        ReplayAction ra;
        ra.turn=m_replayTurn;
        ra.team=teamEq(team,CLR_RED)?0:1;
        ra.type=type;
        if(parent){ra.px=parent->pos.X; ra.py=parent->pos.Y;}
        ra.tx=tgt.X; ra.ty=tgt.Y;
        ra.strength=str; ra.extend=ext;
        ra.scBefore=scBefore; ra.scAfter=scAfter;
        m_replay.push_back(ra);
    }
    // 自动保存对局 (文件名: mode_diff_date_time.btb — mode: pvp/pva/ava, diff: easy/mid/hard)
    void saveReplay(){
        time_t t=time(nullptr);
        struct tm tmv{}; localtime_s(&tmv,&t);
        const char* modeStr = m_bothAI ? "ava" : (m_aiMode ? "pva" : "pvp");
        const char* diffStr = (m_diff==0)?"easy":(m_diff==2)?"hard":"mid";
        if(m_bothAI) diffStr="mid";
        char fn[160];
        snprintf(fn,160,"%s_%s_%04d%02d%02d_%02d%02d%02d.btb",
            modeStr,diffStr,tmv.tm_year+1900,tmv.tm_mon+1,tmv.tm_mday,tmv.tm_hour,tmv.tm_min,tmv.tm_sec);
        FILE* f=nullptr;
        if(fopen_s(&f,fn,"w")!=0||!f) return;
        fprintf(f,"BTBDT1\n");
        fprintf(f,"players=%d\n",m_players);
        fprintf(f,"red_score=%d\n",(int)m_plyScores[0]);
        fprintf(f,"blue_score=%d\n",(int)m_plyScores[1]);
        if(m_players>=3) fprintf(f,"green_score=%d\n",(int)m_plyScores[2]);
        if(m_players>=4) fprintf(f,"yellow_score=%d\n",(int)m_plyScores[3]);
        fprintf(f,"difficulty=%d\n",m_diff);
        const char* w = (m_winner==L"Red")?"red":(m_winner==L"Blue")?"blue"
                      : (m_winner==L"Green")?"green":(m_winner==L"Yellow")?"yellow":"draw";
        fprintf(f,"winner=%s\n",w);
        // 开局黄点布局 ([S] 段: 回放实时统计 + AI 黄点识别的数据源)
        int nSp=0;
        for(auto&sp:m_initScores) if(sp.alive) nSp++;
        fprintf(f,"scores=%d\n",nSp);
        for(auto&sp:m_initScores){
            if(!sp.alive) continue;
            fprintf(f,"[S]\n");
            fprintf(f,"x=%.1f\n",sp.pos.X);
            fprintf(f,"y=%.1f\n",sp.pos.Y);
            fprintf(f,"v=%d\n",sp.value);
        }
        for(auto& ra:m_replay){
            fprintf(f,"[A]\n");
            fprintf(f,"t=%d\n",ra.turn);
            fprintf(f,"tm=%d\n",ra.team);
            fprintf(f,"ty=%d\n",ra.type);
            fprintf(f,"px=%.1f\n",ra.px);
            fprintf(f,"py=%.1f\n",ra.py);
            fprintf(f,"tx=%.1f\n",ra.tx);
            fprintf(f,"ty2=%.1f\n",ra.ty);
            fprintf(f,"s=%d\n",ra.strength);
            fprintf(f,"e=%d\n",ra.extend);
            fprintf(f,"b=%d\n",ra.scBefore);
            fprintf(f,"a=%d\n",ra.scAfter);
            fprintf(f,"nk=%d\n",ra.nodesKilled);
            fprintf(f,"ek=%d\n",ra.edgesKilled);
        }
        fclose(f);
    }

    // ===== 回放 =====
    bool loadReplayFile(const wchar_t* path){
        FILE* f=nullptr;
        if(_wfopen_s(&f,path,L"r")!=0||!f) return false;
        char line[256];
        m_repActs.clear();
        m_repScores.clear();
        m_repRs=10; m_repBs=10; m_repGs=10; m_repYs=10; m_repPlayers=2; m_repWinner="red";
        ReplayAction cur; bool inAct=false;
        ReplayScorePt curSp; bool inSp=false;
        while(fgets(line,sizeof line,f)){
            char key[32]; char val[160];
            if(sscanf_s(line,"%31s",key,sizeof(key))==1 && strcmp(key,"[A]")==0){
                if(inAct) m_repActs.push_back(cur);
                cur=ReplayAction(); inAct=true; inSp=false; continue;
            }
            if(sscanf_s(line,"%31s",key,sizeof(key))==1 && strcmp(key,"[S]")==0){
                if(inSp) m_repScores.push_back(curSp);
                curSp=ReplayScorePt(); inSp=true; inAct=false; continue;
            }
            if(sscanf_s(line,"%31[^=]=%159s",key,sizeof(key),val,sizeof(val))==2){
                if(inSp){
                    if(strcmp(key,"x")==0) curSp.x=(float)atof(val);
                    else if(strcmp(key,"y")==0) curSp.y=(float)atof(val);
                    else if(strcmp(key,"v")==0) curSp.v=atoi(val);
                    continue;
                }
                if(strcmp(key,"players")==0) m_repPlayers=atoi(val);
                else if(strcmp(key,"red_score")==0) m_repRs=atoi(val);
                else if(strcmp(key,"blue_score")==0) m_repBs=atoi(val);
                else if(strcmp(key,"green_score")==0) m_repGs=atoi(val);
                else if(strcmp(key,"yellow_score")==0) m_repYs=atoi(val);
                else if(strcmp(key,"winner")==0) m_repWinner=val;
                else if(strcmp(key,"t")==0) cur.turn=atoi(val);
                else if(strcmp(key,"tm")==0) cur.team=atoi(val);
                else if(strcmp(key,"ty")==0) cur.type=atoi(val);
                else if(strcmp(key,"px")==0) cur.px=(float)atof(val);
                else if(strcmp(key,"py")==0) cur.py=(float)atof(val);
                else if(strcmp(key,"tx")==0) cur.tx=(float)atof(val);
                else if(strcmp(key,"ty2")==0) cur.ty=(float)atof(val);
                else if(strcmp(key,"s")==0) cur.strength=atoi(val);
                else if(strcmp(key,"e")==0) cur.extend=atoi(val);
                else if(strcmp(key,"b")==0) cur.scBefore=atoi(val);
                else if(strcmp(key,"a")==0) cur.scAfter=atoi(val);
                else if(strcmp(key,"nk")==0) cur.nodesKilled=atoi(val);
                else if(strcmp(key,"ek")==0) cur.edgesKilled=atoi(val);
            }
        }
        if(inAct) m_repActs.push_back(cur);
        if(inSp) m_repScores.push_back(curSp);
        fclose(f);
        m_repFile=path;
        return !m_repActs.empty();
    }
    // 打开文件对话框选择 .btb (兼容旧 .btbdt)
    bool pickReplayFile(){
        wchar_t fn[MAX_PATH]={0};
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof ofn;
        ofn.hwndOwner=m_hwnd;
        ofn.lpstrFilter=L"BTB Replay Files\0*.btb;*.btbdt\0All Files\0*.*\0";
        ofn.lpstrFile=fn; ofn.nMaxFile=MAX_PATH;
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        return GetOpenFileNameW(&ofn)!=0 && loadReplayFile(fn);
    }
    // 回放补充黄点: 收集后补一个新球 (与真实游戏一致), 固定种子保证跳转可复现
    void replaySpawnScore(){
        int aliveCount=0;
        for(auto& s:m_scores) if(s.alive) aliveCount++;
        if(aliveCount>=m_settings.maxScorePts) return;   // 按"存活数量"判断, 避免已收集未清理的假点占位
        std::uniform_real_distribution<float> xd(40, WIN_W-40), yd(40, WIN_H-40);
        int vv;
        { int rr=m_rng()%100; if(rr<40)vv=1; else if(rr<70)vv=2; else vv=3; }
        for(int t=0;t<50;++t){
            PointF p{xd(m_rng),yd(m_rng)};
            bool clash=false;
            for(auto& s:m_scores) if(s.alive && len2(p,s.pos)<SP_R*4){clash=true;break;}
            if(clash) continue;
            for(auto* n:m_all) if(len2(p,n->pos)<NODE_R*3){clash=true;break;}
            if(!clash){m_scores.push_back({p,vv,true}); return;}
        }
    }
    // 开始回放 (加载初始局面, V6.2.0: 按回放玩家数放根)
    void startReplay(){
        m_all.clear();
        for(int i=0;i<4;++i) m_roots[i].reset();
        m_players = (m_repPlayers==4) ? 4 : 2;
        m_roots[0]=std::make_unique<Node>(Node{{80,80},CLR_TEAMS[0]});
        m_roots[1]=std::make_unique<Node>(Node{{WIN_W-80.f,WIN_H-80.f},CLR_TEAMS[1]});
        if(m_players>=3) m_roots[2]=std::make_unique<Node>(Node{{WIN_W-80.f,80.f},CLR_TEAMS[2]});
        if(m_players>=4) m_roots[3]=std::make_unique<Node>(Node{{80.f,WIN_H-80.f},CLR_TEAMS[3]});
        for(int i=0;i<m_players;++i) m_all.push_back(m_roots[i].get());
        m_plyScores[0]=m_repRs; m_plyScores[1]=m_repBs; m_plyScores[2]=m_repGs; m_plyScores[3]=m_repYs;
        m_repIdx=0; m_repPaused=true; m_repLastTick=GetTickCount64();
        m_aiThinking=false; m_thinkingAI=nullptr;
        m_rng.seed(0x5EED);   // 固定种子: 补充黄点序列可复现, 跳转进度一致
        // 加载开局黄点 (btbdt [S] 段; 旧文件无则空, 由 replayStep 推断统计)
        m_scores.clear();
        m_repStats = ReplayScoreStats{};
        for(auto& sp:m_repScores)
            m_scores.push_back({ {sp.x,sp.y}, sp.v, true });
        m_repStats.total = (int)m_repScores.size();
        m_repStats.remain = m_repStats.total;
        m_repGhostCollected.clear(); m_repGhostOwners.clear();
        m_state=State::Replay;
    }
    // 应用一个回放行动 (含攻击效果: 削弱/摧毁)
    void replayStep(){
        if(m_repIdx>=(int)m_repActs.size()) return;
        auto& ra=m_repActs[m_repIdx];
        Node* parent=nullptr;
        for(auto*n:m_all)
            if(fabs(n->pos.X-ra.px)<1.f && fabs(n->pos.Y-ra.py)<1.f){parent=n;break;}
        Color team=CLR_TEAMS[ra.team%4];   // V6.2.0: 4 方
        if(ra.type==0 && parent){
            auto nd=std::make_unique<Node>();
            nd->pos={ra.tx,ra.ty}; nd->team=team; nd->parent=parent;
            nd->edgeStrength=ra.strength;
            Node* raw=nd.get();
            parent->children.push_back(std::move(nd));
            m_all.push_back(raw);
            // 攻击效果: 分支穿越敌方节点/边 → 削弱/摧毁 (与真实对局一致)
            PointF src=parent->pos, tgt={ra.tx,ra.ty};
            std::set<Node*> hitNodes, crossEdges;
            for(auto*n:m_all){
                if(teamEq(n->team,team)) continue;
                if(ptEq(n->pos,src)||ptEq(n->pos,tgt)) continue;
                if(ptSegDist(n->pos,src,tgt)<NODE_R+ATK_M) hitNodes.insert(n);
            }
            for(auto*n:m_all){
                if(teamEq(n->team,team)) continue;
                for(auto& c:n->children)
                    if(c && segCross(src,tgt,n->pos,c->pos)) crossEdges.insert(c.get());
            }
            // 命中节点本体: 削弱该节点连接父边的强度, 归零 → 整棵子树摧毁
            for(Node* t:hitNodes){
                if(std::find(m_all.begin(),m_all.end(),t)==m_all.end())continue;
                if(t->removed)continue;
                if(!t->parent){ killSubtree(t); continue; }
                if(--t->edgeStrength<=0) killSubtree(t);
            }
            if(!m_rRoot || !m_bRoot) return;
            // 穿越边: 削弱线段强度, 归零 → 整棵子树摧毁
            for(Node* c:crossEdges){
                if(std::find(m_all.begin(),m_all.end(),c)==m_all.end())continue;
                if(--c->edgeStrength<=0) killSubtree(c);
            }
        } else if(ra.type==1){
            // 强化: px,py 是边子节点位置
            for(auto*n:m_all)
                if(fabs(n->pos.X-ra.px)<1.f && fabs(n->pos.Y-ra.py)<1.f && n->parent){
                    n->edgeStrength=ra.strength; break;
                }
        } else if(ra.type==3){
            // 删除节点 (整棵子树销毁)
            for(auto*n:m_all)
                if(fabs(n->pos.X-ra.px)<1.f && fabs(n->pos.Y-ra.py)<1.f && n->parent){
                    deleteNode(n); break;
                }
        }
        // ===== 得分黄点: 收集 + 实时统计 =====
        if(ra.type==0 && parent){
            PointF tgtP{ra.tx,ra.ty};
            bool got=false;
            for(auto&sp:m_scores)
                if(sp.alive && len2(tgtP,sp.pos)<COLLECT_R){ sp.alive=false; got=true; break; }
            if(got){
                int v=0;
                for(auto&sp:m_repScores)
                    if(len2(tgtP,{sp.x,sp.y})<COLLECT_R){ v=sp.v; break; }
                int t=ra.team%4;
                m_repStats.got[t]++; m_repStats.val[t]+=v;
                m_repStats.remain=std::max(0,m_repStats.total-m_repStats.got[0]-m_repStats.got[1]-m_repStats.got[2]-m_repStats.got[3]);
                replaySpawnScore();   // 收集后补充新黄点 (与真实游戏一致, 回放可见)
            } else if(m_repScores.empty() && ra.scAfter>ra.scBefore){
                // 旧文件无 [S] 段: 分数增加 → 推断幽灵黄点 (实时统计, 位置=落点)
                m_repGhostCollected.push_back(tgtP);
                m_repGhostOwners.push_back(ra.team);
                m_repStats.ghostAdded++;
                m_repStats.total++;
                int gain=ra.scAfter-ra.scBefore;
                int t=ra.team%4;
                m_repStats.got[t]++; m_repStats.val[t]+=gain;
                replaySpawnScore();   // 旧文件同样补充
            }
        }
        if(ra.team<4) m_plyScores[ra.team]=ra.scAfter;
        m_repIdx++;
    }
    // 跳转到指定回放位置 (重建局面并重放, V6.2.0: 按回放玩家数放根)
    void replayJumpTo(int idx){
        if(idx<0) idx=0;
        if(idx>(int)m_repActs.size()) idx=(int)m_repActs.size();
        m_all.clear();
        for(int i=0;i<4;++i) m_roots[i].reset();
        m_players = (m_repPlayers==4) ? 4 : 2;
        m_roots[0]=std::make_unique<Node>(Node{{80,80},CLR_TEAMS[0]});
        m_roots[1]=std::make_unique<Node>(Node{{WIN_W-80.f,WIN_H-80.f},CLR_TEAMS[1]});
        if(m_players>=3) m_roots[2]=std::make_unique<Node>(Node{{WIN_W-80.f,80.f},CLR_TEAMS[2]});
        if(m_players>=4) m_roots[3]=std::make_unique<Node>(Node{{80.f,WIN_H-80.f},CLR_TEAMS[3]});
        for(int i=0;i<m_players;++i) m_all.push_back(m_roots[i].get());
        m_plyScores[0]=m_repRs; m_plyScores[1]=m_repBs; m_plyScores[2]=m_repGs; m_plyScores[3]=m_repYs;
        m_rng.seed(0x5EED);   // 固定种子: 补充黄点序列与从头播放一致
        m_scores.clear();
        m_repStats = ReplayScoreStats{};
        for(auto& sp:m_repScores)
            m_scores.push_back({ {sp.x,sp.y}, sp.v, true });
        m_repStats.total = (int)m_repScores.size();
        m_repStats.remain = m_repStats.total;
        m_repGhostCollected.clear(); m_repGhostOwners.clear();
        m_repIdx=0;
        while(m_repIdx<idx && m_repIdx<(int)m_repActs.size()) replayStep();
    }
    // 回放控制条
    void drawReplayUI(Graphics& g){
        float bw=WIN_W-40.f, bx=20.f, by=WIN_H-34.f, bh=14.f;
        panel(g,bx-8,by-24,bw+16,46,Color(230,30,30,35),Color(160,170,180,200),1.f);
        int total=(int)m_repActs.size();
        float pct = total>0 ? (float)m_repIdx/total : 0.f;
        SolidBrush bg(Color(255,220,224,230));
        g.FillRectangle(&bg,bx,by,bw,bh);
        if(pct>0){
            SolidBrush fill(Color(255,60,160,220));
            g.FillRectangle(&fill,bx,by,bw*pct,bh);
        }
        Pen pbr(Color(180,190,200,210),1.f);
        g.DrawRectangle(&pbr,bx,by,bw,bh);
        // 状态文字: 播放状态 + 当前回合方 + 动作类型
        std::wstring st = m_repPaused ? L"⏸" : L"▶";
        text(g,st,bx+4,by-20,Color(255,60,90,140),18,true);
        if(m_repIdx < (int)m_repActs.size()){
            auto& ra = m_repActs[m_repIdx];
            const Color tcol[4]={Color(255,225,60,70),Color(255,60,140,255),Color(255,46,204,113),Color(255,230,180,40)};
            Color tc = tcol[ra.team%4];
            const wchar_t* tyLbl = L"";
            switch(ra.type){
                case 0:  tyLbl = L"Branch"; break;
                case 1:  tyLbl = L"Reinforce"; break;
                case 2:  tyLbl = L"Extra Move"; break;
                case 3:  tyLbl = L"Delete"; break;
                case 4:  tyLbl = L"Reconnect"; break;
                case 5:  tyLbl = L"Upgrade Node"; break;
                default: tyLbl = L""; break;
            }
            const wchar_t* whoName[4]={L"RED",L"BLUE",L"GREEN",L"YELLOW"};
            std::wstring who = whoName[ra.team%4];
            text(g, who + L" · " + tyLbl, bx+30, by-20, tc, 16, true);
            text(g, L"Turn "+std::to_wstring(ra.turn+1)+L"   Step "+std::to_wstring(m_repIdx+1)+L" / "+std::to_wstring(total),
                 bx+bw-250, by-20, Color(255,80,80,90), 14);
        } else {
            text(g, L"END", bx+30, by-20, Color(255,120,130,150), 16, true);
            text(g, L"Step "+std::to_wstring(total)+L" / "+std::to_wstring(total),
                 bx+bw-250, by-20, Color(255,80,80,90), 14);
        }
        // ===== 当前动作高亮 (提示将要发生的操作) =====
        if(m_repIdx < (int)m_repActs.size()){
            auto& ra = m_repActs[m_repIdx];
            const Color hl[4]={Color(255,255,60,70),Color(255,60,150,255),Color(255,46,204,113),Color(255,230,180,40)};
            Color hc = hl[ra.team%4];
            float pulse = 0.5f + 0.5f*std::sin(GetTickCount64()*0.008f);
            BYTE A=(BYTE)(120+pulse*70);
            if(ra.type==0){
                // 分支: 起终点连线预览
                line(g,{ra.px,ra.py},{ra.tx,ra.ty},Color(A,hc.GetR(),hc.GetG(),hc.GetB()),2.5f);
                circle(g,{ra.tx,ra.ty},NODE_R+4+pulse*3,Color(A,hc.GetR(),hc.GetG(),hc.GetB()),hc,2.5f);
            } else if(ra.type==1){
                // 强化边: 高亮该边 (子节点在 px,py)
                Node* cn=nullptr;
                for(auto*n:m_all)
                    if(fabs(n->pos.X-ra.px)<1.f && fabs(n->pos.Y-ra.py)<1.f){cn=n;break;}
                if(cn && cn->parent)
                    line(g,cn->parent->pos,cn->pos,Color(A,hc.GetR(),hc.GetG(),hc.GetB()),2.5f);
                circle(g,{ra.px,ra.py},NODE_R+4+pulse*3,Color(A,hc.GetR(),hc.GetG(),hc.GetB()),hc,2.5f);
            } else {
                // 删除/加强节点/额外行动: 高亮目标节点
                PointF tgt{ra.px,ra.py};
                circle(g,tgt,NODE_R+5+pulse*3,Color(A,hc.GetR(),hc.GetG(),hc.GetB()),hc,2.5f);
            }
        }
        // 右上角: 速度显示 (大字体)
        wchar_t sp[64]; swprintf_s(sp,64,L"Speed: %.1fs/turn",m_repSpeed);
        text(g,sp,WIN_W-220.f,6,Color(255,60,90,140),20,true);
        // ===== 得分黄点实时统计面板 (右侧上下居中, 避开地图区) =====
        {
            auto& st=m_repStats;
            float spx=WIN_W+20.f, spy=WIN_H/2.f-40.f, spw=PANEL_W-40.f, sph=82.f;
            panel(g,spx,spy,spw,sph,Color(235,24,24,28),Color(170,180,190,210),1.f);
            wchar_t buf[128];
            std::wstring title=L"◆ Score Points: "+std::to_wstring(st.total)+L" total";
            if(st.ghostAdded>0) title+=L"  (+"+std::to_wstring(st.ghostAdded)+L" inf)";
            text(g,title,spx+8,spy+6,Color(255,220,170,40),14,true);
            // 各方收集/剩余 (V6.2.0: 4 方)
            const Color scol[4]={Color(255,220,80,90),Color(255,60,150,255),Color(255,46,204,113),Color(255,230,180,40)};
            const wchar_t* sn[4]={L"Red",L"Blue",L"Green",L"Yellow"};
            for(int k=0;k<4;++k){
                float px = spx + 8 + (k%2)*92.f;
                float pyy = spy + 30 + (k/2)*16.f;
                if(m_repPlayers>=2 || k<2){
                    swprintf_s(buf,128,L"%s: %d (+%d)",sn[k],st.got[k],st.val[k]);
                    text(g,buf,px,pyy,scol[k],12,true);
                }
            }
            swprintf_s(buf,128,L"Left: %d",st.remain);
            text(g,buf,spx+8,spy+62,Color(255,120,170,110),12,true);
        }
        // 操作提示
        text(g,L"Space:Pause  <-/->:Speed  Esc:Exit",bx+bw-330,by+16,Color(255,140,150,165),13);
    }
    void checkVictory(){
        // V6.2.0: 多阵营淘汰制 — 统计存活根; 2人时存活剩1即结束, 4人时逐方淘汰到剩1
        int alive=0, winnerIdx=-1;
        for(int i=0;i<m_players;++i)
            if(rootAlive(m_roots[i].get())){ alive++; winnerIdx=i; }
        if(alive<=1){
            m_over=true;
            m_winner = (alive==1) ? (winnerIdx==0?L"Red":winnerIdx==1?L"Blue":winnerIdx==2?L"Green":L"Yellow")
                                  : L"Nobody";
            m_state=State::GameOver;
        }
        if(m_over && !m_replaySaved){   // 按模式决定是否自动保存对局
            m_replaySaved=true;
            bool wantSave = m_bothAI ? m_settings.saveAva
                          : (m_aiMode ? m_settings.savePva : m_settings.savePvp);
            if(wantSave) saveReplay();
        }
    }

    // ===== 快照 / 回退 =====
    struct SnapNode{PointF pos;Color team;int parent=-1;std::vector<int>children;int edgeStrength=1;};
    struct Snap{std::vector<SnapNode>nodes;int rIdx=0,bIdx=0;int rScore=0,bScore=0;
        std::vector<ScorePoint>scores;Color turn;bool didBranch=false;};
    std::vector<Snap> m_history;
    std::vector<std::unique_ptr<Node>> m_graveyard;   // 已死父节点 (持有孤立子节点)

    void saveState(){
        std::map<Node*,int> idx;
        for(int i=0;i<(int)m_all.size();i++)idx[m_all[i]]=i;
        Snap s; s.nodes.resize(m_all.size());
        for(int i=0;i<(int)m_all.size();i++){
            Node*n=m_all[i];
            s.nodes[i]={n->pos,n->team,n->parent?idx[n->parent]:-1,{},n->edgeStrength};
            for(auto&c:n->children)s.nodes[i].children.push_back(idx[c.get()]);
        }
        s.rIdx=idx[m_rRoot.get()]; s.bIdx=idx[m_bRoot.get()];
        s.rScore=(int)m_rScore; s.bScore=(int)m_bScore; s.scores=m_scores;
        s.turn=m_turn; s.didBranch=m_didBranch;
        m_history.push_back(std::move(s));
        if(m_history.size()>200)m_history.erase(m_history.begin());
    }
    void restoreState(){
        if(m_history.empty())return;
        Snap&s=m_history.back();
        m_all.clear(); m_sel=m_hover=nullptr; clearReinf();
        m_rRoot.reset(); m_bRoot.reset();
        std::vector<std::unique_ptr<Node>> pool(s.nodes.size());
        std::vector<Node*> raw(s.nodes.size());
        for(size_t i=0;i<s.nodes.size();i++){
            pool[i]=std::make_unique<Node>();
            pool[i]->pos=s.nodes[i].pos; pool[i]->team=s.nodes[i].team;
            pool[i]->edgeStrength=s.nodes[i].edgeStrength; raw[i]=pool[i].get();
        }
        for(size_t i=0;i<s.nodes.size();i++)
            if(s.nodes[i].parent>=0)raw[i]->parent=raw[s.nodes[i].parent];
        for(size_t i=0;i<s.nodes.size();i++)
            for(int ci:s.nodes[i].children)
                raw[i]->children.push_back(std::move(pool[ci]));
        m_rRoot=std::move(pool[s.rIdx]); m_bRoot=std::move(pool[s.bIdx]);
        std::function<void(Node*)> collect=[&](Node*n){
            m_all.push_back(n); for(auto&c:n->children)collect(c.get());
        };
        collect(m_rRoot.get()); collect(m_bRoot.get());
        m_rScore=s.rScore; m_bScore=s.bScore; m_scores=s.scores;
        m_turn=s.turn; m_didBranch=s.didBranch;
        m_history.pop_back();
    }

    // ===== AI =====
    Node* myRootOf(const Color& c){ return rootOf(idxOfTeam(c)); }
    Node* enRootOf(const Color& c){ return rootOf((idxOfTeam(c)+1)%2); }   // AI 只用于 2 人模式
    SecureInt& scoreOf(const Color& c){ return m_plyScores[idxOfTeam(c)]; } // V6.2.0: 4 阵营通用
    int enScoreOf(const Color& c){ return (int)(teamEq(c,CLR_RED)?m_bScore:m_rScore); }

    // ===== AI 配置 (ai_*.dat) =====
    // 扫描当前目录所有 ai_*.dat (文件名前缀 ai_ 即可, 内容符合规范)
    // 不自动生成: 没有 dat 时列表为空, 由菜单提示无法对战
    void scanReasoners(){
        m_reasoners.clear();
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA("ai_*.dat", &fd);
        if(h != INVALID_HANDLE_VALUE){
            do {
                if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    m_reasoners.push_back(fd.cFileName);
            } while(FindNextFileA(h, &fd));
            FindClose(h);
        }
        // 按文件名排序 (ai_reasoner_0001 < ai_reasoner_0002 < ... < ai_zzz)
        std::sort(m_reasoners.begin(), m_reasoners.end());
        if(!m_reasoners.empty()){
            m_aiRed.loadFromFile(m_reasoners[0].c_str());
            m_aiBlue.loadFromFile(m_reasoners[0].c_str());
        }
    }
    std::string reasonerPath(int idx){
        if(idx<0 || idx>=(int)m_reasoners.size()) return "ai_reasoner_0001.dat";
        return m_reasoners[idx];
    }
    // 把 reasoner 配置载入指定 AI, 并标记已加载(退出时回写)
    void loadReasonerInto(AI& ai, int idx, bool& flag){
        std::string p = reasonerPath(idx);
        ai.loadFromFile(p.c_str());
        flag = true;
    }
    // 退出时: 把本局用到的 reasoner 回写 (该 AI 内存中的配置/学习数据即来自该文件, 直接写回)
    void saveReasonersOnExit(){
        if(m_redReasonerLoaded)  m_aiRed.saveToFile(reasonerPath(m_redReasoner).c_str());
        if(m_blueReasonerLoaded) m_aiBlue.saveToFile(reasonerPath(m_blueReasoner).c_str());
    }

    // 边界吸附: 靠近边界时自动贴到边界内缘
    PointF snapPos(PointF p){
        if(!m_snapEnabled) return p;
        if(p.X < SNAP_EDGE) p.X = SNAP_EDGE;
        else if(p.X > WIN_W - SNAP_EDGE) p.X = WIN_W - SNAP_EDGE;
        if(p.Y < SNAP_EDGE) p.Y = SNAP_EDGE;
        else if(p.Y > WIN_H - SNAP_EDGE) p.Y = WIN_H - SNAP_EDGE;
        return p;
    }

    // 开始 AI 思考 (选点动画即时显示)
    void startThink(const Color& team, AI& ai){
        if(m_didBranch)return;
        saveState();
        if(m_over)return;
        int scVal = scoreOf(team);                       // 解密
        // AI 防守强化: 记录到回放 (每条 type=1, 含积分变化)
        {
            int scPre = scVal;
            std::map<Node*,int> before;
            for(auto*n : m_all)
                if(teamEq(n->team, team) && n->parent && !n->isolated && !n->removed)
                    before[n] = n->edgeStrength;
            ai.reinforce(m_all, scVal, enScoreOf(team), team); // 先防守强化
            int run = scPre;
            for(auto& kv : before){
                Node* ch = kv.first;
                if(ch->edgeStrength > kv.second){
                    int up = ch->edgeStrength - kv.second;
                    if(up>0){
                        recordAction(1, ch, ch->pos, ch->edgeStrength, 0, run, run-up, team);
                        run -= up;
                    }
                }
            }
        }
        scoreOf(team) = scVal;                           // 加密写回
        ai.setDifficulty(m_diff);                        // 设置 AI 难度
        // 思考时间按难度: Easy 短 / Normal 中 / Hard 长
        if(m_diff==2){ m_minThink=5200; m_maxThink=8500; }
        else if(m_diff==0){ m_minThink=1800; m_maxThink=3200; }
        else { m_minThink=2500; m_maxThink=4800; }
        ai.beginThink(m_all,myRootOf(team),enRootOf(team),scVal,enScoreOf(team),m_scores,team);
        m_thinkingAI=&ai;
        m_thinkingTeam=team;
        m_aiThinking=true;
        m_aiExtrasUsed=0;
        m_aiExtraPending=false;
        m_thinkStart=GetTickCount64();
        m_lastThinkTick=GetTickCount64();
    }

    // 执行一次移动 (创建分支); netEcho=true 时(在线模式本机动作)发送给对方
    void executeMove(const Color& team, const AI::Move& mv, bool netEcho=false){
        if(!mv.parent||mv.score<=-9000)return;
        SecureInt& sc=scoreOf(team);
        if(sc<0) sc=0;   // 防御: 分数不为负
        int cost=mv.extend*m_settings.extendCost+(mv.strength-DEF_S)*m_settings.reinfCost;
        if(cost>sc)return;
        int scBefore=(int)sc;
        sc-=cost;
        auto nd=std::make_unique<Node>();
        nd->pos=mv.target; nd->team=team; nd->parent=mv.parent;
        nd->edgeStrength=mv.strength;
        Node* raw=nd.get();
        mv.parent->children.push_back(std::move(nd));
        m_all.push_back(raw);
        collectAt(mv.target);
        AttackPreview ap=previewAttack(mv.parent->pos,mv.target,team);
        processAttack(mv.parent->pos,mv.target);
        m_didBranch=true; checkVictory();
        // 记录行动
        recordAction(0, mv.parent, mv.target, mv.strength, mv.extend,
                     scBefore, (int)sc, team);
        // 攻击战果
        if(!m_replay.empty()){
            m_replay.back().nodesKilled=ap.nodesHit;
            m_replay.back().edgesKilled=ap.edgesKilled;
        }
        // 在线模式: 本机动作发送给对方
        if(netEcho && m_netActive && m_netSock!=INVALID_SOCKET){
            char buf[256];
            snprintf(buf,sizeof buf,
                "MOVE tm=%d ty=0 px=%.1f py=%.1f tx=%.1f ty2=%.1f s=%d e=%d b=%d a=%d nk=%d ek=%d\n",
                idxOfTeam(team), mv.parent->pos.X, mv.parent->pos.Y,
                mv.target.X, mv.target.Y, mv.strength, mv.extend, scBefore, (int)sc,
                ap.nodesHit, ap.edgesKilled);
            netSend(buf);
        }
    }

    // ===== 联机对战 (在线 PvP) =====
    void netSend(const std::string& msg){
        if(m_netSock==INVALID_SOCKET) return;
        send(m_netSock, msg.c_str(), (int)msg.size(), 0);
    }
    // 解析 host:port → host/port (IPv4 only)
    void parseHostPort(const std::string& hp, std::string& host, std::string& port){
        host=hp; port="8080";
        size_t colon=hp.rfind(':');
        if(colon!=std::string::npos){
            host=hp.substr(0,colon);
            port=hp.substr(colon+1);
            if(host.empty()) host="127.0.0.1";
        }
    }
    // 连接房间服务器 (出站, 无需开放入站端口)
    bool netConnectServer(const std::string& serverAddr){
        std::string host, port;
        parseHostPort(serverAddr, host, port);
        addrinfo hints{}, *res=nullptr;
        hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
        int gai=getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
        if(gai!=0){ m_netErrMsg="Cannot resolve server \""+host+"\""; return false; }
        SOCKET s=INVALID_SOCKET; int lastErr=0;
        for(auto* p=res; p; p=p->ai_next){
            s=socket(p->ai_family,p->ai_socktype,p->ai_protocol);
            if(s==INVALID_SOCKET){ lastErr=WSAGetLastError(); continue; }
            u_long mode=1; ioctlsocket(s,FIONBIO,&mode);
            int r=connect(s,p->ai_addr,(int)p->ai_addrlen);
            if(r!=0){
                int e=WSAGetLastError();
                if(e==WSAEWOULDBLOCK){
                    fd_set wf; FD_ZERO(&wf); FD_SET(s,&wf); timeval tv{8,0};
                    r=select(0,nullptr,&wf,nullptr,&tv);
                    if(r<=0){ lastErr=(r==0)?WSAETIMEDOUT:e; closesocket(s); s=INVALID_SOCKET; continue; }
                    int soe=0, sl=sizeof soe; getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&soe,&sl);
                    if(soe!=0){ lastErr=soe; closesocket(s); s=INVALID_SOCKET; continue; }
                }else{ lastErr=e; closesocket(s); s=INVALID_SOCKET; continue; }
            }
            break;
        }
        freeaddrinfo(res);
        if(s==INVALID_SOCKET){
            m_netErrMsg = (lastErr==WSAETIMEDOUT) ? "Connect to server timed out (check address)"
                          : "Connect to server failed (error "+std::to_string(lastErr)+")";
            return false;
        }
        u_long nm=1; ioctlsocket(s,FIONBIO,&nm);
        m_netSock=s; m_netBuf.clear();
        m_netActive=false; m_netGotSeed=false; m_netOppLeft=false;
        for(int i=0;i<4;++i) m_netPlayerColors[i]=-1;
        m_netRoomFull=false; m_netJoined=1;
        return true;
    }
    // 开房: 连服务器 → CREATE <n> → 服务器返回 ROOM 房间码
    bool netHost(const std::string& serverAddr){
        if(!netConnectServer(serverAddr)) return false;
        m_netRole=0; m_netColor=0; m_netWaiting=true; m_netPlayerIdx=0;
        m_netServer="Connecting..."; m_netErrMsg.clear();
        m_netPlayerColors[0]=m_netColor; m_netJoined=1;
        char buf[32]; snprintf(buf,sizeof buf,"CREATE %d\n",m_netRoomSize);
        netSend(buf);
        m_state=State::Playing;
        return true;
    }
    // 加入: 输入 "服务器地址 房间码" → 连服务器 → JOIN <code>
    bool netJoin(const std::string& serverAndRoom){
        std::string server=serverAndRoom, room;
        size_t sp=serverAndRoom.rfind(' ');
        if(sp!=std::string::npos){ server=serverAndRoom.substr(0,sp); room=serverAndRoom.substr(sp+1); }
        if(server.empty()||room.empty()){ m_netErrMsg="Enter: serverAddr roomCode  (e.g. host:8080 1234)"; return false; }
        if(!netConnectServer(server)) return false;
        m_netRole=1; m_netColor=1; m_netWaiting=true; m_netPlayerIdx=-1;
        m_netServer="Joined "+server+" room "+room; m_netErrMsg.clear();
        m_netJoined=1; m_netPlayerColors[0]=-1;
        netSend("JOIN "+room+"\n");
        m_state=State::Playing;
        return true;
    }
    void netDisconnect(){
        if(m_netSock!=INVALID_SOCKET){ closesocket(m_netSock); m_netSock=INVALID_SOCKET; }
        m_netActive=false; m_netWaiting=false; m_netMyTurn=false; m_netOppLeft=false; m_netRoomFull=false;
    }
    void netOppLeft(){ m_netOppLeft=true; netDisconnect(); }
    // 房主: 当房间满员且各玩家颜色唯一时, 广播开局并本地开局
    void hostTryStart(){
        if(m_netRole!=0) return;
        int need = m_netRoomSize;
        if(need!=2 && need!=4) need=2;
        // 房间是否满员 (房主自己 + 已加入位图)
        bool full = (m_netJoined & ((1<<need)-1)) == ((1<<need)-1);
        if(!full) return;
        // 各玩家颜色是否唯一
        bool seen[4]={false,false,false,false};
        for(int j=0;j<need;++j){
            int c=m_netPlayerColors[j];
            if(c<0 || c>3 || seen[c]) return;
            seen[c]=true;
        }
        // 发起开局
        char mb[24]; snprintf(mb,sizeof mb,"MAPSIZE %d\n",m_settings.mapIdx); netSend(mb);
        char cm[48]; snprintf(cm,sizeof cm,"COLORMAP %d %d %d %d\n",
            m_netPlayerColors[0],m_netPlayerColors[1],m_netPlayerColors[2],m_netPlayerColors[3]);
        netSend(cm);
        netSend("START\n");
        netStartPeer();
    }
    // 在线开局: 按玩家编号摆根 (颜色=角), 玩家0(房主)先手并发送黄点种子
    void netStartPeer(){
        m_aiMode=false; m_bothAI=false; m_netActive=true;
        m_players = (m_netRoomSize==4)?4:2;
        m_netMyTurn = (m_netPlayerIdx==0);   // 房主先手
        m_netWaiting=false;
        m_all.clear(); m_scores.clear(); m_history.clear();
        for(int i=0;i<4;++i) m_roots[i].reset();
        int corners[4][2] = {{80,80},{WIN_W-80,WIN_H-80},{WIN_W-80,80},{80,WIN_H-80}};
        for(int j=0;j<m_players;++j){
            int c = m_netPlayerColors[j];
            if(c<0 || c>3) c = j%4;
            m_roots[c] = std::make_unique<Node>(Node{{(float)corners[c][0],(float)corners[c][1]}, CLR_TEAMS[c]});
            m_all.push_back(m_roots[c].get());
        }
        m_turn=CLR_TEAMS[(m_netPlayerColors[0]>=0)?m_netPlayerColors[0]:m_netColor];
        m_sel=m_hover=nullptr; m_extend=0; m_str=DEF_S;
        m_reinf=nullptr; m_reinfStr=0; m_nodeMenu=nullptr; m_didBranch=false; m_xUsedThisTurn=false;
        m_plyScores[0]=START_SCORE; m_plyScores[1]=START_SCORE; m_plyScores[2]=START_SCORE; m_plyScores[3]=START_SCORE;
        m_over=false; m_winner.clear();
        m_replay.clear(); m_replayTurn=0; m_replaySaved=false;
        m_aiThinking=false; m_thinkingAI=nullptr; m_hoverEdge=nullptr;
        m_state=State::Playing; m_enterClock.Restart(); m_aiClock.Restart();
        // 黄点同步: 主机生成种子并发送, 加入方等待 SEED
        if(m_netRole==0){
            unsigned seed=(unsigned)time(nullptr)^0x5EEDu;
            m_rng.seed(seed);
            for(int i=0;i<m_settings.initScorePts;++i) spawnScore();
            m_initScores=m_scores;
            char buf[64]; snprintf(buf,sizeof buf,"SEED %u\n",seed);
            netSend(buf);
            m_netGotSeed=true;
        } else {
            m_netGotSeed=false;
        }
    }
    // 处理一行网络消息 (服务器转发, 带 "PEER " 前缀)
    void netHandleLine(const std::string& line){
        bool fromPeer=false;
        std::string msg=line;
        if(msg.rfind("PEER ",0)==0){ fromPeer=true; msg=msg.substr(5); }
        if(msg.rfind("BYE",0)==0){ netOppLeft(); return; }
        if(msg.rfind("ROOM ",0)==0){ m_netServer="Room code: "+msg.substr(5); return; }   // 开房成功
        if(msg.rfind("JOINED ",0)==0){
            int idx=atoi(msg.c_str()+7);
            if(fromPeer){                       // 其他玩家加入了房间
                if(m_netRole==0 && idx>=1 && idx<4) m_netJoined |= (1<<idx);
            } else {                            // 服务器告诉我的编号
                m_netPlayerIdx=idx;
            }
            return;
        }
        if(msg.rfind("FULL ",0)==0){
            m_netRoomSize=atoi(msg.c_str()+5);
            if(m_netRoomSize!=2 && m_netRoomSize!=4) m_netRoomSize=2;
            m_netRoomFull=true;
            if(m_netRole==0) hostTryStart();
            return;
        }
        if(msg.rfind("COLOR ",0)==0){           // 服务器已去重, 格式: PEER COLOR <玩家编号> <颜色>
            int pidx=0,cidx=0;
            sscanf(msg.c_str()+6,"%d %d",&pidx,&cidx);
            if(pidx>=0 && pidx<4) m_netPlayerColors[pidx]=cidx;
            if(m_netRole==0){ m_netPlayerColors[0]=m_netColor; hostTryStart(); }
            return;
        }
        if(msg.rfind("COLORMAP ",0)==0){        // 房主广播的最终颜色表
            int c0=0,c1=0,c2=0,c3=0;
            sscanf(msg.c_str()+9,"%d %d %d %d",&c0,&c1,&c2,&c3);
            m_netPlayerColors[0]=c0; m_netPlayerColors[1]=c1;
            m_netPlayerColors[2]=c2; m_netPlayerColors[3]=c3;
            return;
        }
        if(msg.rfind("MAPSIZE ",0)==0){
            int idx=atoi(msg.c_str()+8);
            if(idx>=0 && idx<=4){ m_settings.mapIdx=idx; applyMapSize(); saveSettings(); }
            return;
        }
        if(msg=="START"){ netStartPeer(); return; }
        if(msg=="TAKECOLOR"){
            m_netColor=(m_netColor+1)%4;
            char b[24]; snprintf(b,sizeof b,"COLOR %d\n",m_netColor); netSend(b);
            if(m_netRole==0){ m_netPlayerColors[0]=m_netColor; }
            return;
        }
        if(msg.rfind("SEED ",0)==0 && !m_netGotSeed){
            unsigned seed=(unsigned)atoi(msg.c_str()+5);
            m_rng.seed(seed);
            for(int i=0;i<m_settings.initScorePts;++i) spawnScore();
            m_initScores=m_scores;
            m_netGotSeed=true;
            return;
        }
        if(msg=="ENDTURN"){
            m_didBranch=false; m_xUsedThisTurn=false;
            advanceTurn();
            m_netMyTurn = teamEq(m_turn, CLR_TEAMS[m_netColor%4]);
            m_enterClock.Restart();
            return;
        }
        if(msg.rfind("EXTRA ",0)==0){           // 某玩家购买了额外行动, 同步扣分
            int tm=atoi(msg.c_str()+6);
            if(tm>=0 && tm<4){
                SecureInt& sc=m_plyScores[tm%4];
                if((int)sc>=EXTRA_COST) sc-=EXTRA_COST;
            }
            return;
        }
        if(msg.rfind("MOVE ",0)==0){
            int tm=0,ty=0,s=1,e=0; float px=0,py=0,tx=0,ty2=0;
            sscanf(msg.c_str()+5, "tm=%d ty=%d px=%f py=%f tx=%f ty2=%f s=%d e=%d", &tm,&ty,&px,&py,&tx,&ty2,&s,&e);
            Color team=CLR_TEAMS[tm%4];
            if(ty==0){
                Node* parent=nullptr;
                for(auto*n:m_all)
                    if(teamEq(n->team,team)&&fabs(n->pos.X-px)<1.f&&fabs(n->pos.Y-py)<1.f){parent=n;break;}
                if(!parent) return;
                AI::Move mv; mv.parent=parent; mv.target={tx,ty2}; mv.strength=s; mv.extend=e; mv.score=1.f;
                executeMove(team,mv,false);
            } else if(ty==1){
                Node* child=nullptr;
                for(auto*n:m_all)
                    if(teamEq(n->team,team)&&fabs(n->pos.X-tx)<1.f&&fabs(n->pos.Y-ty2)<1.f&&n->parent){child=n;break;}
                if(child && child->edgeStrength<MAX_S){
                    SecureInt& sc=scoreOf(team);
                    int cost=(s-child->edgeStrength)*m_settings.reinfCost;
                    if(cost>0 && (int)sc>=cost){ sc-=cost; child->edgeStrength=s; recordAction(1,child,child->pos,s,0,(int)sc+cost,(int)sc,team); }
                }
            } else if(ty==3){
                Node* n=nullptr;
                for(auto* nd:m_all)
                    if(teamEq(nd->team,team)&&fabs(nd->pos.X-px)<1.f&&fabs(nd->pos.Y-py)<1.f&&nd->parent){n=nd;break;}
                if(n) deleteNode(n);
            }
            return;
        }
    }
    // 每帧轮询服务器收包
    void netPoll(){
        if(m_netSock==INVALID_SOCKET) return;
        char tmp[2048];
        int n=recv(m_netSock,tmp,sizeof tmp,0);
        if(n>0) m_netBuf.append(tmp,n);
        else if(n==0){ netOppLeft(); return; }
        size_t pos;
        while((pos=m_netBuf.find('\n'))!=std::string::npos){
            std::string line=m_netBuf.substr(0,pos);
            m_netBuf.erase(0,pos+1);
            netHandleLine(line);
        }
    }
    // 思考完成: 执行最佳移动, 可选额外行动, 切换回合
    void finishThink(){
        Color team=m_thinkingTeam;
        AI& ai=*m_thinkingAI;
        SecureInt& sc=scoreOf(team);
        auto mv=ai.result();
        // ===== 第二次思考 (额外行动尝试): 评估是否值得执行, 否则退还3分 =====
        if(m_aiExtraPending){
            m_aiExtraPending=false;
            Node* enR0 = enRootOf(team);   // 可能已被 executeMove 杀根置空
            // 只有高价值(杀根/冲刺推进到敌根近区)才执行, 否则退还3分
            bool worth = (mv.score > 1e8f) ||   // findKillMove 杀根
                         (mv.parent && enR0 && mv.score > 600.f &&
                          len2(mv.target, enR0->pos) < 220.f);  // 冲刺推进
            if(worth && !m_over){
                executeMove(team, mv);
            } else {
                sc += 3;   // 退还不值得的额外行动
            }
            if(!m_over){ advanceTurn();m_didBranch=false; m_xUsedThisTurn=false; m_enterClock.Restart(); m_replayTurn++;}
            m_aiThinking=false; m_thinkingAI=nullptr;
            return;
        }
        // ===== 第一次思考: 正常执行 =====
        executeMove(team,mv);
        // 额外行动: 仅当存在"杀根窗口"才买 — 即己方有可扩展节点已贴脸到敌根
        // (数据: 人类只在杀根前夜用X; 平时乱买3分是浪费)
        bool earlyGame = (m_all.size() < 8);   // 开局省分
        Node* enR = enRootOf(team);   // executeMove 可能已杀根置空 → 判空防崩溃
        bool killWindow = false;
        if(enR && !m_over){
            for(auto* n : m_all){
                if(teamEq(n->team, team) && n->children.size() < 2 &&
                   len2(n->pos, enR->pos) < ai.cfg().extraSprint){
                    killWindow = true; break;
                }
            }
        }
        if(!m_over && sc>=6 && !earlyGame && killWindow && m_aiExtrasUsed==0){
            sc-=3; m_aiExtrasUsed++; m_aiExtraPending=true;
            ai.beginThink(m_all,myRootOf(team),enRootOf(team),sc,enScoreOf(team),m_scores,team);
            m_thinkStart=GetTickCount64();   // 继续思考第二次
            m_lastThinkTick=GetTickCount64();
            InvalidateRect(m_hwnd,nullptr,FALSE);
            return;                          // 保持 m_aiThinking=true, 继续第二次思考
        }
        // 结束回合
        if(!m_over){ advanceTurn();m_didBranch=false; m_xUsedThisTurn=false; m_enterClock.Restart(); m_replayTurn++;}
        m_aiThinking=false; m_thinkingAI=nullptr;
    }

    // ===================== 插件 AI (ai_plugins\*.dll) =====================
    // 把当前局面序列化为 AIPluginState (每次决策前重建)
    void buildPluginState(AIPluginState& st,
                          std::vector<AIPluginNode>& anode,
                          std::vector<AIPluginScorePoint>& apts,
                          std::vector<Node*>& id2node,
                          const Color& team){
        std::map<Node*,int> idx;
        anode.clear(); id2node.clear();
        for(auto* n : m_all){
            idx[n]=(int)id2node.size();
            AIPluginNode an; memset(&an,0,sizeof an);
            an.id=(int)id2node.size();
            an.team=teamEq(n->team,CLR_RED)?0:1;
            an.x=n->pos.X; an.y=n->pos.Y;
            an.edgeStrength=n->edgeStrength;
            an.level=n->level;
            an.isolated=n->isolated?1:0;
            anode.push_back(an);
            id2node.push_back(n);
        }
        for(int i=0;i<(int)id2node.size();++i){
            Node* n=id2node[i];
            anode[i].parentId=(n->parent && idx.count(n->parent))?idx[n->parent]:-1;
            int k=0;
            for(auto& c : n->children){
                if(k>=2) break;
                if(idx.count(c.get())) anode[i].children[k++]=idx[c.get()];
            }
            anode[i].childCount=k;
        }
        apts.clear();
        for(auto& sp : m_scores){
            if(!sp.alive) continue;
            AIPluginScorePoint ap;
            ap.x=sp.pos.X; ap.y=sp.pos.Y; ap.value=sp.value; ap.alive=1;
            apts.push_back(ap);
        }
        st.apiVersion=AI_PLUGIN_API_VERSION;
        st.nodeCount=(int)anode.size();
        st.nodes=anode.empty()?nullptr:anode.data();
        st.scorePointCount=(int)apts.size();
        st.scorePoints=apts.empty()?nullptr:apts.data();
        st.redScore=(int)m_rScore;
        st.blueScore=(int)m_bScore;
        st.myTeam=teamEq(team,CLR_RED)?0:1;
        st.maxBranchLength=MAX_D+3*EXTRA_D;
        st.nodeRadius=NODE_R;
        st.occupyRadius=OCCUPY_R;
        st.mapWidth=WIN_W;
        st.mapHeight=WIN_H;
    }
    // 执行插件返回的一步 (校验 + 落地 + 记录回放)
    bool pluginExecuteMove(const AIPluginMove& mv, const std::vector<Node*>& id2node){
        auto nodeAt=[&](int id)->Node*{
            if(id<0||id>=(int)id2node.size()) return nullptr;
            return id2node[id];
        };
        if(mv.action==AI_ACT_BRANCH){
            Node* p=nodeAt(mv.parentId);
            if(!p || !teamEq(p->team,m_turn) || p->isolated || p->children.size()>=2) return false;
            PointF tgt{mv.targetX,mv.targetY};
            float d=len2(p->pos,tgt);
            int cost=mv.extend*m_settings.extendCost+(mv.strength-DEF_S)*m_settings.reinfCost;
            SecureInt& sc=scoreOf(m_turn);
            if(d>20 && d<=MAX_D+mv.extend*EXTRA_D && mv.strength>=DEF_S && mv.strength<=MAX_S
               && mv.extend>=0 && mv.extend<=3 && cost>=0 && cost<=(int)sc
               && tgt.X>=10 && tgt.X<=WIN_W-10 && tgt.Y>=10 && tgt.Y<=WIN_H-10){
                bool occ=false;
                for(auto*n:m_all) if(n!=p && len2(n->pos,tgt)<OCCUPY_R){occ=true;break;}
                if(occ) return false;
                saveState();
                int scBefore=(int)sc;
                sc-=cost;
                auto nd=std::make_unique<Node>();
                nd->pos=tgt; nd->team=m_turn; nd->parent=p; nd->edgeStrength=mv.strength;
                Node* raw=nd.get();
                p->children.push_back(std::move(nd));
                m_all.push_back(raw);
                AttackPreview ap=previewAttack(p->pos,tgt,m_turn);
                collectAt(tgt); processAttack(p->pos,tgt);
                m_didBranch=true; checkVictory();
                recordAction(0,p,tgt,mv.strength,mv.extend,scBefore,(int)sc,m_turn);
                if(!m_replay.empty()){ m_replay.back().nodesKilled=ap.nodesHit; m_replay.back().edgesKilled=ap.edgesKilled; }
                return true;
            }
            return false;
        } else if(mv.action==AI_ACT_REINF_EDGE){
            Node* c=nodeAt(mv.targetId);
            if(!c || !teamEq(c->team,m_turn) || !c->parent || c->isolated) return false;
            int ns=mv.strength; if(ns<1)ns=1; if(ns>MAX_S)ns=MAX_S;
            int cost=ns-c->edgeStrength;
            SecureInt& sc=scoreOf(m_turn);
            if(cost>0 && cost<=(int)sc){
                int scBefore=(int)sc;
                sc-=cost; c->edgeStrength=ns;
                recordAction(1,c,c->pos,ns,0,scBefore,(int)sc,m_turn);
                return true;
            }
            return false;
        }
        // AI_ACT_UPGRADE_NODE: 节点加强机制已移除, 该动作不再生效
        return false;
    }
    // 插件回合流程: 循环 getMove → 执行, 直到分支或无效; 支持额外行动
    void startPluginThink(const Color& team){
        int idx = teamEq(team,CLR_RED) ? m_redPlugin : m_bluePlugin;
        if(idx<0 || idx>=(int)m_plugins.size()) return;
        AIPlugin& pl=m_plugins[idx];
        if(!pl.loaded || !pl.fnGetMove) return;
        for(int step=0; step<24; ++step){
            std::vector<AIPluginNode> anode;
            std::vector<AIPluginScorePoint> apts;
            std::vector<Node*> id2node;
            AIPluginState st;
            buildPluginState(st,anode,apts,id2node,team);
            if(pl.fnThinkStart) pl.fnThinkStart(&st);
            AIPluginMove mv; memset(&mv,0,sizeof mv);
            if(!pl.fnGetMove(&st,&mv)) break;
            if(!mv.valid) break;
            if(!pluginExecuteMove(mv,id2node)) break;
            if(mv.action==AI_ACT_BRANCH){
                // 额外行动: 买 3 分再走一步 (仅分支可触发)
                if(mv.buyExtra && !m_over && (int)scoreOf(team)>=EXTRA_COST){
                    SecureInt& sc=scoreOf(team);
                    int scBefore=(int)sc;
                    sc-=EXTRA_COST; m_didBranch=false;
                    recordAction(2,nullptr,{0.f,0.f},0,0,scBefore,(int)sc,team);
                    std::vector<AIPluginNode> a2;
                    std::vector<AIPluginScorePoint> p2;
                    std::vector<Node*> id2n2;
                    AIPluginState st2;
                    buildPluginState(st2,a2,p2,id2n2,team);
                    AIPluginMove mv2; memset(&mv2,0,sizeof mv2);
                    if(pl.fnGetMove(&st2,&mv2) && mv2.valid && mv2.action==AI_ACT_BRANCH){
                        pluginExecuteMove(mv2,id2n2);
                    }
                    m_didBranch=true;
                }
                break;
            }
            // 强化/加强节点: 不消耗回合, 继续问插件下一步
        }
        // 结束回合
        if(!m_over){ advanceTurn(); m_didBranch=false; m_xUsedThisTurn=false; m_enterClock.Restart(); m_replayTurn++; }
        InvalidateRect(m_hwnd,nullptr,FALSE);
    }
    // 拉取插件上报的候选落点 (热力图调试用)
    void populatePluginCands(const Color& team){
        int idx = teamEq(team,CLR_RED) ? m_redPlugin : m_bluePlugin;
        if(idx<0 || idx>=(int)m_plugins.size()) return;
        AIPlugin& pl=m_plugins[idx];
        if(!pl.loaded || !pl.fnGetCands) return;
        std::vector<AIPluginNode> anode;
        std::vector<AIPluginScorePoint> apts;
        std::vector<Node*> id2node;
        AIPluginState st;
        buildPluginState(st,anode,apts,id2node,team);
        m_pluginCands.clear();
        m_pluginCands.resize(AI_PLUGIN_MAX_CANDS);
        int n = pl.fnGetCands(&st, m_pluginCands.data(), AI_PLUGIN_MAX_CANDS);
        if(n<0) n=0;
        if(n>(int)m_pluginCands.size()) n=(int)m_pluginCands.size();
        m_pluginCands.resize(n);
    }
    // 渲染插件候选热力图 (蓝→黄→红, 值越大越红)
    void drawPluginHeat(Graphics& g){
        if(m_pluginCands.empty()) return;
        float mn=1e9f,mx=-1e9f;
        for(auto&c:m_pluginCands){mn=std::min(mn,c.score);mx=std::max(mx,c.score);}
        float range=mx-mn; if(range<0.001f) range=1.f;
        auto heat=[&](float t)->Color{
            t=std::max(0.f,std::min(1.f,t));
            if(t<0.5f){float u=t*2.f; return Color(200,(BYTE)(60+u*120),(BYTE)(150+u*80),(BYTE)(255-u*200));}
            float u=(t-0.5f)*2.f; return Color(210,(BYTE)(180+u*75),(BYTE)(230-u*140),(BYTE)(55-u*55));
        };
        textC(g, L"Plugin candidate heatmap",
              WIN_W/2.f, 14, Color(255,120,120,130), 13, true);
        for(auto&c : m_pluginCands){
            float t=(c.score-mn)/range;
            float rad=6+t*12;
            Color col=heat(t);
            SolidBrush br(Color(150,col.GetR(),col.GetG(),col.GetB()));
            g.FillEllipse(&br,c.x-rad,c.y-rad,rad*2,rad*2);
            Pen pn(Color(220,col.GetR(),col.GetG(),col.GetB()),1.5f);
            g.DrawEllipse(&pn,c.x-rad,c.y-rad,rad*2,rad*2);
            int pct=(int)(t*100.f);
            if(pct>=20){
                std::wstring s=std::to_wstring(pct)+L"%";
                textC(g,s,c.x,c.y-3,Color(255,30,30,30),10,true);
            }
        }
    }

    // ===== 输入 =====
    Node* findEdgeAt(PointF m){
        Node*best=nullptr; float bd=12;
        for(auto*n:m_all){
            if(!teamEq(n->team,m_turn))continue;
            for(auto&c:n->children){
                float d=ptSegDist(m,n->pos,c->pos);
                if(d<bd){bd=d;best=c.get();}
            }
        }
        return best;
    }
    void onPress(PointF m){
        // 回放: 点击进度条跳转
        if(m_state==State::Replay){
            float bx=20.f, by=WIN_H-34.f, bw=WIN_W-40.f;
            if(m.Y>=by-12.f && m.Y<=by+26.f && m.X>=bx && m.X<=bx+bw){
                float pct=std::max(0.f,std::min(1.f,(m.X-bx)/bw));
                replayJumpTo((int)(pct*m_repActs.size()));
                m_repPaused=true;   // 跳转后暂停查看
                m_repDrag=true;
            }
            return;
        }
        if(m_state!=State::Playing||m_over)return;
        if(m_aiThinking)return;   // AI 思考中禁止玩家操作
        if(m_netActive&&!m_netMyTurn)return;   // 在线: 非本机回合禁止操作
        if(m_didBranch)return;
        for(auto*n:m_all)
            if(len2(n->pos,m)<NODE_R*2&&teamEq(n->team,m_turn)&&n->children.size()<2){
                m_nodeMenu=nullptr; clearReinf();m_sel=n;m_extend=0;m_str=DEF_S;return;
            }
    }
    void onRPress(PointF m){
        if(m_state!=State::Playing||m_over)return;
        if(m_aiThinking)return;   // AI 思考中禁止右键
        if(m_netActive&&!m_netMyTurn)return;   // 在线: 非本机回合禁止操作
        if(m_reinf){clearReinf();return;}
        if(m_sel){clearDrag();return;}
        // 右键己方节点 → 普通节点弹操作面板; 孤立节点提示拖拽接回
        for(auto*n:m_all){
            if(!teamEq(n->team,m_turn))continue;
            if(n->removed)continue;
            if(len2(n->pos,m)<NODE_R*2){
                if(n->isolated){
                    MessageBoxW(m_hwnd,L"Isolated node - drag a nearby node onto it to reconnect (Snap ON)",
                                L"Reconnect",MB_OK);
                } else if(n->parent){
                    m_nodeMenu = n;   // 弹操作面板: 2删除
                }
                return;
            }
        }
        auto*e=findEdgeAt(m); if(e){m_reinf=e;m_reinfStr=e->edgeStrength;}
    }
    void onRelease(PointF m){
        (void)m;
        if(m_state==State::Replay){ m_repDrag=false; return; }  // 结束进度条拖拽
        m=snapPos(m);  // 边界吸附
        if(m_state!=State::Playing||m_over||m_aiThinking)return;  // AI 思考中禁止落子
        if(m_netActive&&!m_netMyTurn)return;   // 在线: 非本机回合禁止落子
        if(std::find(m_all.begin(),m_all.end(),m_sel)==m_all.end()||m_sel->children.size()>=2){clearDrag();return;}
        float d=len2(m_sel->pos,m);
        float maxD=MAX_D+m_extend*EXTRA_D;
        int cost=m_extend*m_settings.extendCost+(m_str-DEF_S)*m_settings.reinfCost;
        SecureInt& sc=scoreOf(m_turn);
        bool occ=false;
        for(auto*n:m_all)if(n!=m_sel&&len2(n->pos,m)<OCCUPY_R){occ=true;break;}
        if(d>20&&d<maxD&&!occ&&sc>=cost){
            saveState();
            int scBefore=(int)sc;
            sc-=cost;
            auto nd=std::make_unique<Node>();
            nd->pos=m; nd->team=m_turn; nd->parent=m_sel; nd->edgeStrength=m_str;
            Node*raw=nd.get();
            m_sel->children.push_back(std::move(nd));
            m_all.push_back(raw);
            PointF src=m_sel->pos;
            AttackPreview ap=previewAttack(src,m,m_turn);
            collectAt(m); processAttack(src,m);
            m_didBranch=true; checkVictory();
            // 记录行动
            recordAction(0, m_sel, m, m_str, m_extend, scBefore, (int)sc, m_turn);
            if(!m_replay.empty()){
                m_replay.back().nodesKilled=ap.nodesHit;
                m_replay.back().edgesKilled=ap.edgesKilled;
            }
            // 在线模式: 发送落子给对方
            if(m_netActive){
                char buf[256];
                snprintf(buf,sizeof buf,
                    "MOVE tm=%d ty=0 px=%.1f py=%.1f tx=%.1f ty2=%.1f s=%d e=%d b=%d a=%d nk=%d ek=%d\n",
                    idxOfTeam(m_turn), src.X, src.Y, m.X, m.Y,
                    m_str, m_extend, scBefore, (int)sc, ap.nodesHit, ap.edgesKilled);
                netSend(buf);
            }
            // 玩家策略学习: 判断本次行动类型 (仅 vs AI 模式, 玩家=蓝方)
            if(m_aiMode&&!m_bothAI&&teamEq(m_turn,CLR_BLUE)){
                float dEnemy=len2(m, myRootOf(CLR_BLUE)->pos);
                m_aiRed.observePlayerAction(dEnemy<280?2:0);
            }
        }
        clearDrag();
    }
    void onWheel(float d){
        if(!m_sel||m_state!=State::Playing||m_aiThinking)return;
        if(m_netActive&&!m_netMyTurn)return;
        SecureInt& sc=scoreOf(m_turn);
        if(d>0){if(m_str<MAX_S&&m_str-DEF_S<sc)++m_str;}
        else{if(m_str>DEF_S)--m_str;}
    }
    void onKey(UINT vk){
        bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        // 主界面
        if(m_state==State::Menu){
            if(m_menuPhase==0){   // ---- 模式选择 ----
                if(vk==VK_DOWN){ m_menuSel=(m_menuSel+1)%5; return; }
                if(vk==VK_UP){   m_menuSel=(m_menuSel+4)%5; return; }
                if(vk==VK_RETURN&&m_enterClock.GetElapsedTime()>0.5){
                    if(m_menuSel==4){           // 选 Settings → 设置页
                        m_menuSel=0; m_menuPhase=8; m_enterClock.Restart(); return;
                    }
                    if(m_menuSel==3){           // 选 Replay → 打开文件对话框
                        if(pickReplayFile()) startReplay();
                        return;
                    }
                    if(m_menuSel==1){           // 选 vs AI → 进入 AI 配置(dat)列表
                        m_redPlugin=-1; m_redDiff=1;
                        m_redReasonerLoaded=false; m_blueReasonerLoaded=false;
                        scanReasoners();                    // 每次进入扫描
                        m_menuPhase=1; m_menuSel=0; m_enterClock.Restart(); return;
                    }
                    if(m_menuSel==0){           // 选 PvP → 子菜单 (本机/在线)
                        m_redReasonerLoaded=false; m_blueReasonerLoaded=false;
                        m_menuPhase=3; m_menuSel=0; m_enterClock.Restart(); return;
                    }
                    // 选 AI Battle (m_menuSel==2) → 进入红方 AI 配置(dat)列表
                    m_redPlugin=-1; m_redDiff=1;
                    m_bluePlugin=-1; m_blueDiff=1;
                    m_redReasonerLoaded=false; m_blueReasonerLoaded=false;
                    scanReasoners();
                    m_menuPhase=4; m_menuSel=0; m_enterClock.Restart();
                }
                return;
            }else if(m_menuPhase==3){   // ---- PvP 子菜单: 本机2人 / 本机4人 / 在线 ----
                if(vk==VK_DOWN){ m_menuSel=(m_menuSel+1)%3; return; }
                if(vk==VK_UP){   m_menuSel=(m_menuSel+2)%3; return; }
                if(vk==VK_ESCAPE){ m_menuPhase=0; m_menuSel=0; m_enterClock.Restart(); return; }
                if(vk==VK_RETURN&&m_enterClock.GetElapsedTime()>0.5){
                    m_aiMode=false; m_bothAI=false; m_diff=1;
                    m_redPlugin=-1; m_bluePlugin=-1;
                    if(m_menuSel==0){           // 本机 2 人对战
                        m_players=2; m_menuPhase=0; initGame();
                    }else if(m_menuSel==1){     // 本机 4 人对战 (四角)
                        m_players=4; m_menuPhase=0; initGame();
                    }else{                      // 在线 → 子菜单 (开房/加入)
                        m_menuPhase=12; m_menuSel=0; m_enterClock.Restart();
                    }
                    return;
                }
                return;
            }else if(m_menuPhase==12){  // ---- 在线子菜单: 人数 / 开房(Host) / 加入(Join) ----
                if(vk==VK_DOWN){ m_menuSel=(m_menuSel+1)%3; return; }
                if(vk==VK_UP){   m_menuSel=(m_menuSel+2)%3; return; }
                if(vk==VK_ESCAPE){ m_menuPhase=3; m_menuSel=2; m_enterClock.Restart(); return; }
                if((vk==VK_LEFT||vk==VK_RIGHT)&&m_menuSel==0){
                    m_netRoomSize=(m_netRoomSize==2)?4:2;
                    m_enterClock.Restart();
                    return;
                }
                if(vk==VK_RETURN&&m_enterClock.GetElapsedTime()>0.5){
                    m_aiMode=false; m_bothAI=false; m_diff=1;
                    m_redPlugin=-1; m_bluePlugin=-1;
                    if(m_menuSel==0){           // 切换人数
                        m_netRoomSize=(m_netRoomSize==2)?4:2;
                        m_enterClock.Restart();
                        return;
                    }
                    if(m_menuSel==1){           // 开房: 输入服务器地址 → CREATE <n> 房间
                        std::wstring defAddr = L"127.0.0.1:8080";
                        if(m_settings.vpnEnable && !m_settings.vpnIP.empty())
                            defAddr = std::wstring(m_settings.vpnIP.begin(),m_settings.vpnIP.end())+L":8080";
                        EnableWindow(m_hwnd, FALSE);
                        std::string srv=promptInput(L"Enter room server address (IPv4)", defAddr.c_str());
                        EnableWindow(m_hwnd, TRUE);
                        if(netHost(srv)) m_state=State::Playing;   // 进入 Waiting, 等 ROOM 码
                        else MessageBoxW(m_hwnd,(L"Host failed: "+std::wstring(m_netErrMsg.begin(),m_netErrMsg.end())).c_str(),L"Host",MB_OK|MB_ICONWARNING);
                    }else{                      // 加入: 输入 "服务器地址 房间码" → JOIN
                        std::wstring defAddr = L"127.0.0.1:8080 ";
                        if(m_settings.vpnEnable && !m_settings.vpnIP.empty())
                            defAddr = std::wstring(m_settings.vpnIP.begin(),m_settings.vpnIP.end())+L":8080 ";
                        EnableWindow(m_hwnd, FALSE);
                        std::string addr=promptInput(L"Enter: serverAddr roomCode  (e.g. 192.168.1.5:8080 1234)", defAddr.c_str());
                        EnableWindow(m_hwnd, TRUE);
                        if(!addr.empty()){
                            if(netJoin(addr)) m_state=State::Playing;
                            else{
                                std::wstring emsg(m_netErrMsg.begin(), m_netErrMsg.end());
                                MessageBoxW(m_hwnd, (L"Failed to join!\n\n"+emsg).c_str(),
                                            L"Online Battle", MB_OK|MB_ICONWARNING);
                            }
                        }
                    }
                }
                return;
            }else if(m_menuPhase==1 || m_menuPhase==4 || m_menuPhase==6){
                // ---- AI 配置文件(dat)列表: 1=vs AI红方, 4=AI Battle红方, 6=AI Battle蓝方 ----
                int& curReasoner = (m_menuPhase==6) ? m_blueReasoner : m_redReasoner;
                int cnt=(int)m_reasoners.size();
                if(cnt==0){                     // 无 dat → 无法对战
                    if(vk==VK_ESCAPE){ m_menuPhase=0; m_menuSel=(m_menuPhase==4||m_menuPhase==6)?2:1; m_enterClock.Restart(); return; }
                    return;
                }
                if(vk==VK_DOWN){ m_menuSel=(m_menuSel+1)%cnt; return; }
                if(vk==VK_UP){   m_menuSel=(m_menuSel+cnt-1)%cnt; return; }
                if(vk==VK_ESCAPE){ m_menuPhase=0; m_menuSel=(m_menuPhase==4||m_menuPhase==6)?2:1; m_enterClock.Restart(); return; }
                if(vk==VK_RETURN&&m_enterClock.GetElapsedTime()>0.5){
                    curReasoner = m_menuSel;
                    if(m_menuPhase==1){         // vs AI → 红方难度/插件
                        m_redPlugin=-1; m_redDiff=1;
                        m_menuPhase=2; m_menuSel=1; m_enterClock.Restart();
                    }else if(m_menuPhase==4){   // AI Battle 红方 → 红方难度/插件
                        m_redPlugin=-1; m_redDiff=1;
                        m_menuPhase=5; m_menuSel=1; m_enterClock.Restart();
                    }else{                      // AI Battle 蓝方 → 蓝方难度/插件
                        m_bluePlugin=-1; m_blueDiff=1;
                        m_menuPhase=7; m_menuSel=1; m_enterClock.Restart();
                    }
                }
                return;
            }else if(m_menuPhase==2 || m_menuPhase==5 || m_menuPhase==7){
                // ---- 难度/插件选择: 2=vs AI红方, 5=AI Battle红方, 7=AI Battle蓝方 ----
                bool isBlue = (m_menuPhase==7);
                int shown = std::min((int)m_plugins.size(), 3);
                int cnt = 3 + shown;
                if(vk==VK_DOWN){ m_menuSel=(m_menuSel+1)%cnt; return; }
                if(vk==VK_UP){   m_menuSel=(m_menuSel+cnt-1)%cnt; return; }
                if(vk==VK_ESCAPE){ // 返回重选 dat
                    m_menuPhase = isBlue?6:(m_menuPhase==5?4:1);
                    m_menuSel = isBlue?m_blueReasoner:m_redReasoner;
                    m_enterClock.Restart(); return;
                }
                if(vk==VK_RETURN&&m_enterClock.GetElapsedTime()>0.5){
                    int& plugin = isBlue?m_bluePlugin:m_redPlugin;
                    int& diff   = isBlue?m_blueDiff:m_redDiff;
                    if(m_menuSel>=3){ plugin=m_menuSel-3; diff=1; }
                    else { plugin=-1; diff=m_menuSel; }
                    AI& ai = isBlue?m_aiBlue:m_aiRed;
                    int reasoner = isBlue?m_blueReasoner:m_redReasoner;
                    bool& loaded = isBlue?m_blueReasonerLoaded:m_redReasonerLoaded;
                    if(plugin<0) loadReasonerInto(ai, reasoner, loaded);   // 内置 → 载入所选 dat
                    if(m_menuPhase==2){         // vs AI → 开始
                        m_aiMode=true; m_bothAI=false; m_menuPhase=0; initGame();
                    }else if(m_menuPhase==5){   // AI Battle 红方 → 选蓝方 dat
                        scanReasoners();
                        m_menuPhase=6; m_menuSel=0; m_enterClock.Restart();
                    }else{                      // AI Battle 蓝方 → 开始
                        m_aiMode=false; m_bothAI=true; m_menuPhase=0; initGame();
                    }
                }
                return;
            }else if(m_menuPhase==8){   // ---- 设置页 ----
                // 行: 0=地图尺寸 1=保存对局 2=游戏规则 3..5=快捷键开关 6=虚拟组网
                if(vk==VK_ESCAPE){ m_menuPhase=0; m_menuSel=4; m_enterClock.Restart(); saveSettings(); return; }
                if(vk==VK_DOWN){ m_menuSel=(m_menuSel+1)%7; return; }
                if(vk==VK_UP){   m_menuSel=(m_menuSel+6)%7; return; }
                if(vk==VK_RETURN&&m_enterClock.GetElapsedTime()>0.5){
                    if(m_menuSel==0){               // 地图尺寸 → 二级菜单
                        m_menuPhase=9; m_menuSel=m_settings.mapIdx; m_enterClock.Restart(); return;
                    }
                    if(m_menuSel==1){               // 保存对局 → 二级菜单 (分模式)
                        m_menuPhase=10; m_menuSel=0; m_enterClock.Restart(); return;
                    }
                    if(m_menuSel==2){               // 游戏规则 → 二级菜单 (数值)
                        m_menuPhase=11; m_menuSel=0; m_enterClock.Restart(); return;
                    }
                    if(m_menuSel==6){               // 虚拟组网 → 二级菜单
                        m_menuPhase=13; m_menuSel=0; m_enterClock.Restart(); return;
                    }
                    // 快捷键开关: 仅按 Enter 切换 (3=B吸附 4=Ctrl+Z撤销 5=R深度)
                    bool* b = (m_menuSel==3)?&m_settings.snapEnabled
                           : (m_menuSel==4)?&m_settings.undoEnabled
                           : &m_settings.depthEnabled;
                    *b = !*b;
                    if(m_menuSel==3) m_snapEnabled = m_settings.snapEnabled;
                    if(m_menuSel==5) m_showDepth   = m_settings.depthEnabled;
                    saveSettings();
                    m_enterClock.Restart();
                    return;
                }
                return;
            }else if(m_menuPhase==13){  // ---- 虚拟组网 (EasyTier/VPN) 二级菜单 ----
                if(vk==VK_ESCAPE){ m_menuPhase=8; m_menuSel=6; m_enterClock.Restart(); saveSettings(); return; }
                if(vk==VK_DOWN){ m_menuSel=(m_menuSel+1)%6; return; }
                if(vk==VK_UP){   m_menuSel=(m_menuSel+5)%6; return; }
                if(vk==VK_RETURN&&m_enterClock.GetElapsedTime()>0.5){
                    if(m_menuSel==0){               // 启用开关
                        m_settings.vpnEnable=!m_settings.vpnEnable;
                        saveSettings(); m_enterClock.Restart(); return;
                    }
                    if(m_menuSel==5){               // 一键启动 EasyTier
                        launchVpn(); m_enterClock.Restart(); return;
                    }
                    const wchar_t* title=nullptr;
                    std::string* target=nullptr;
                    if(m_menuSel==1){ title=L"EasyTier network name"; target=&m_settings.vpnName; }
                    else if(m_menuSel==2){ title=L"Network secret (key)"; target=&m_settings.vpnSecret; }
                    else if(m_menuSel==3){ title=L"Local virtual IP (e.g. 10.144.0.1)"; target=&m_settings.vpnIP; }
                    else if(m_menuSel==4){ title=L"Custom launch command (empty=default)"; target=&m_settings.vpnCmd; }
                    if(target){
                        std::wstring wdef(target->begin(), target->end());
                        EnableWindow(m_hwnd, FALSE);
                        std::string r=promptInput(title, wdef.c_str());
                        EnableWindow(m_hwnd, TRUE);
                        if(!r.empty() || m_menuSel==3) *target=r;   // IP 允许清空
                        saveSettings(); m_enterClock.Restart();
                    }
                    return;
                }
                return;
            }else if(m_menuPhase==11){  // ---- 游戏规则二级菜单 (←/→ 调整数值) ----
                if(vk==VK_ESCAPE){ m_menuPhase=8; m_menuSel=2; m_enterClock.Restart(); saveSettings(); return; }
                if(vk==VK_DOWN){ m_menuSel=(m_menuSel+1)%5; return; }
                if(vk==VK_UP){   m_menuSel=(m_menuSel+4)%5; return; }
                if(vk==VK_LEFT||vk==VK_RIGHT){
                    int dir=(vk==VK_RIGHT)?1:-1;
                    int* v = (m_menuSel==0)?&m_settings.extendMax
                           : (m_menuSel==1)?&m_settings.extendCost
                           : (m_menuSel==2)?&m_settings.reinfCost
                           : (m_menuSel==3)?&m_settings.maxScorePts
                           : &m_settings.initScorePts;
                    int lo = (m_menuSel==0)?0:(m_menuSel==1||m_menuSel==2)?0:3;
                    int hi = (m_menuSel==0)?5:(m_menuSel==1||m_menuSel==2)?3:(m_menuSel==3)?10:5;
                    *v = std::max(lo,std::min(hi,*v+dir));
                    saveSettings();
                    return;
                }
                return;
            }else if(m_menuPhase==9){   // ---- 分辨率二级菜单 ----
                if(vk==VK_ESCAPE){ m_menuPhase=8; m_menuSel=0; m_enterClock.Restart(); return; }
                if(vk==VK_DOWN){ m_menuSel=(m_menuSel+1)%5; return; }
                if(vk==VK_UP){   m_menuSel=(m_menuSel+4)%5; return; }
                if(vk==VK_RETURN&&m_enterClock.GetElapsedTime()>0.5){
                    m_settings.mapIdx = m_menuSel;
                    applyMapSize();
                    saveSettings();
                    m_menuPhase=8; m_menuSel=0; m_enterClock.Restart();
                    return;
                }
                return;
            }else if(m_menuPhase==10){  // ---- 保存对局二级菜单 (分模式) ----
                if(vk==VK_ESCAPE){ m_menuPhase=8; m_menuSel=1; m_enterClock.Restart(); return; }
                if(vk==VK_DOWN){ m_menuSel=(m_menuSel+1)%3; return; }
                if(vk==VK_UP){   m_menuSel=(m_menuSel+2)%3; return; }
                if(vk==VK_RETURN&&m_enterClock.GetElapsedTime()>0.5){
                    bool* b = (m_menuSel==0)?&m_settings.savePvp
                           : (m_menuSel==1)?&m_settings.savePva
                           : &m_settings.saveAva;
                    *b = !*b;
                    saveSettings();
                    m_enterClock.Restart();
                    return;
                }
                return;
            }
        }
        // 在线等待/选色 (房间服务器: 1-4 选色, Esc 退出)
        if(m_state==State::Playing && m_netWaiting){
            if(vk>='1'&&vk<='4'){
                m_netColor=vk-'1';
                if(m_netSock!=INVALID_SOCKET){
                    char b[24]; snprintf(b,sizeof b,"COLOR %d\n",m_netColor); netSend(b);
                    if(m_netPlayerIdx>=0 && m_netPlayerIdx<4) m_netPlayerColors[m_netPlayerIdx]=m_netColor;
                    if(m_netRole==0) hostTryStart();
                }
                return;
            }
            if(vk==VK_ESCAPE){ netDisconnect(); m_state=State::Menu; return; }
            return;
        }
        // 回放状态按键
        if(m_state==State::Replay){
            if(vk==VK_SPACE){ m_repPaused=!m_repPaused; m_repLastTick=GetTickCount64(); return; }
            if(vk==VK_LEFT) { m_repSpeed=std::max(0.5f,m_repSpeed-0.5f); return; }
            if(vk==VK_RIGHT){ m_repSpeed=std::min(5.f,m_repSpeed+0.5f); return; }
            if(vk==VK_RETURN && m_repPaused){ replayStep(); m_repLastTick=GetTickCount64(); return; }  // 单步
            if(vk==VK_ESCAPE){ m_state=State::Menu; return; }
            return;
        }
        // 结算
        if(m_state==State::GameOver){
            if(vk=='R'&&m_restartClock.GetElapsedTime()>1.5){
                if(m_netActive) netSend("BYE\n");
                netDisconnect();
                m_state=State::Menu;m_over=false;
            }
            return;
        }
        // 在线等待配对: 仅 Esc 退出
        if(m_netWaiting){
            if(vk==VK_ESCAPE){ netDisconnect(); m_state=State::Menu; }
            return;
        }
        // 游戏中
        if(vk=='R'&&ctrl){
            if(m_netActive) netSend("BYE\n");   // 在线: 通知对方退出
            netDisconnect();
            m_state=State::Menu;m_over=false;return;
        }
        if(m_settings.depthEnabled && vk=='R'&&!ctrl){m_showDepth=!m_showDepth;return;}   // R: 显示/隐藏节点深度
        // AI 思考中: 仅允许无害键 (B 吸附开关 / Ctrl+R 回菜单 / R 深度), 其余忽略
        if(m_aiThinking){
            if(m_settings.snapEnabled && vk=='B'){m_snapEnabled=!m_snapEnabled;return;}
            if(m_settings.depthEnabled && vk=='R'&&!ctrl){m_showDepth=!m_showDepth;return;}
            return;
        }
        // 在线: 非本机回合禁止操作 (除 Ctrl+R 退出)
        if(m_netActive&&!m_netMyTurn){
            if(m_settings.snapEnabled && vk=='B'){m_snapEnabled=!m_snapEnabled;}
            return;
        }
        if(m_nodeMenu){
            // 节点操作面板: 2=删除 Esc=取消 (节点加强机制已移除)
            if(vk==VK_ESCAPE){m_nodeMenu=nullptr;return;}
            if(vk=='2'||vk=='D'){
                Node* n=m_nodeMenu; m_nodeMenu=nullptr;
                if(n&&n->parent){
                    int ask=MessageBoxW(m_hwnd,L"Delete this node?\n(Subtree destroyed, refund half of edge reinforcement)",
                                        L"Delete Node",MB_YESNO|MB_ICONQUESTION);
                    if(ask==IDYES){
                        deleteNode(n);
                        if(m_netActive){
                            char buf[96];
                            snprintf(buf,sizeof buf,"MOVE tm=%d ty=3 px=%.1f py=%.1f tx=%.1f ty2=%.1f s=0 e=0\n",
                                idxOfTeam(m_turn),n->pos.X,n->pos.Y,n->pos.X,n->pos.Y);
                            netSend(buf);
                        }
                    }
                }
                return;
            }
            return;   // 菜单打开时拦截其他键
        }
        if(m_reinf){
            SecureInt& sc=scoreOf(m_turn);
            if(vk==VK_ESCAPE){clearReinf();return;}
            if(vk==VK_RETURN){
                int cost=(m_reinfStr-m_reinf->edgeStrength)*m_settings.reinfCost;
                if(cost>0&&sc>=cost){
                    int scBefore=(int)sc;
                    saveState();sc-=cost;m_reinf->edgeStrength=m_reinfStr;
                    // 记录强化
                    recordAction(1, m_reinf, m_reinf->pos, m_reinfStr, 0,
                                 scBefore, (int)sc, m_turn);
                    // 在线: 发送强化
                    if(m_netActive){
                        char buf[160];
                        snprintf(buf,sizeof buf,
                            "MOVE tm=%d ty=1 px=%.1f py=%.1f tx=%.1f ty2=%.1f s=%d e=0\n",
                            idxOfTeam(m_turn),
                            m_reinf->parent?m_reinf->parent->pos.X:0,
                            m_reinf->parent?m_reinf->parent->pos.Y:0,
                            m_reinf->pos.X, m_reinf->pos.Y, m_reinfStr);
                        netSend(buf);
                    }
                    if(m_aiMode&&!m_bothAI&&teamEq(m_turn,CLR_BLUE)) m_aiRed.observePlayerAction(1);
                }
                clearReinf();return;
            }
            int num=-1;
            if(vk>='1'&&vk<='5')num=vk-'1';
            if(num>=0&&num<=4){
                int tgt=num+1;
                if(tgt>=m_reinf->edgeStrength&&tgt<=MAX_S){
                    int cost=tgt-m_reinf->edgeStrength;
                    if(cost<=sc)m_reinfStr=tgt;
                }
            }
            return;
        }
        // 空格: 距离扩展 0..extendMax 级 (费用可配)
        if(vk==VK_SPACE&&m_sel){
            SecureInt& sc=scoreOf(m_turn);
            int nxt=(m_extend+1>m_settings.extendMax)?0:m_extend+1;
            if(sc>=nxt*m_settings.extendCost)m_extend=nxt;
        }
        if(vk==VK_ESCAPE&&m_sel)m_extend=0;
        // B: 边界吸附开关
        if(vk=='B'){ m_snapEnabled=!m_snapEnabled; return; }
        // X: 3分换额外行动 (每回合最多 1 次)
        if(vk=='X'&&!m_xUsedThisTurn&&m_didBranch&&!m_sel&&!m_reinf){
            SecureInt& sc=scoreOf(m_turn);
            if(sc>=EXTRA_COST){
                int scBefore=(int)sc;
                saveState();sc-=EXTRA_COST;m_didBranch=false;
                m_xUsedThisTurn=true;
                // 记录额外行动
                recordAction(2, m_sel, m_mouse, 0, 0, scBefore, (int)sc, m_turn);
                if(m_aiMode&&!m_bothAI&&teamEq(m_turn,CLR_BLUE)) m_aiRed.observePlayerAction(3);
                // 在线: 通知所有客户端同步扣 3 分
                if(m_netActive){
                    char eb[24]; snprintf(eb,sizeof eb,"EXTRA %d\n",idxOfTeam(m_turn));
                    netSend(eb);
                }
            }
            return;
        }
        // Ctrl+Z 回退 (在线模式禁用: 会破坏双方同步)
        if(m_settings.undoEnabled && vk=='Z'&&ctrl&&!m_netActive){restoreState();return;}
        // Enter 结束回合
        if(vk==VK_RETURN&&!m_sel&&!m_reinf){
            if(m_enterClock.GetElapsedTime()<0.3)return;
            if(m_netActive){
                // 在线: 发送回合结束, 各端本地推进回合并判定是否本机行动
                netSend("ENDTURN\n");
                advanceTurn();
                m_netMyTurn = teamEq(m_turn, CLR_TEAMS[m_netColor%4]);
            }else{
                advanceTurn();
            }
            m_didBranch=false; m_xUsedThisTurn=false; m_enterClock.Restart(); m_replayTurn++;
        }
    }
    void clearDrag(){m_sel=nullptr;m_extend=0;m_str=DEF_S;}
    void clearReinf(){m_reinf=nullptr;m_reinfStr=0;}

    // ===== 渲染 (GDI+) =====
    void paint(){
        PAINTSTRUCT ps;
        HDC hdc=BeginPaint(m_hwnd,&ps);
        RECT rc; GetClientRect(m_hwnd,&rc);
        int w=rc.right-rc.left,h=rc.bottom-rc.top;
        // 双缓冲 (缓存 Bitmap, 避免大分辨率下每帧重新分配)
        if(!m_buffer || m_bufW!=w || m_bufH!=h){
            m_buffer = std::make_unique<Bitmap>(w,h,PixelFormat32bppARGB);
            m_bufW=w; m_bufH=h;
        }
        Bitmap& buffer=*m_buffer;
        {
            Graphics g(&buffer);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.Clear(CLR_BG);
            switch(m_state){
            case State::Menu:    drawMenu(g); break;
            case State::Playing: drawGame(g); break;
            case State::GameOver:drawGame(g); drawOverlay(g); break;
            case State::Replay:  drawGame(g); drawReplayUI(g); break;
            }
        }
        Graphics g2(hdc);
        g2.DrawImage(&buffer,0,0,w,h);
        EndPaint(m_hwnd,&ps);
    }

    // 选择可用字体 (GDI+ 字体验证, 避免静默失败)
    void initFont(){
        const wchar_t* cands[]={L"Microsoft YaHei UI",L"Microsoft YaHei",L"SimHei",
                                L"Segoe UI",L"Arial"};
        for(auto* f:cands){
            FontFamily fam(f);
            WCHAR nm[LF_FACESIZE];
            if(fam.GetFamilyName(nm)==Ok && fam.IsAvailable()){
                m_fontName=f; return;
            }
        }
        m_fontName=L"Arial";
    }
    // 字体缓存: GDI+ Font 创建开销大, 按 (字号,粗体) 复用, 避免每帧创建数百个
    Font* cachedFont(float sz,bool bold){
        static std::map<std::pair<int,bool>,Font*> cache;
        auto key=std::make_pair((int)(sz*2.f+0.5f),bold);
        auto it=cache.find(key);
        if(it==cache.end()){
            Font* f=new Font(m_fontName.c_str(),sz,bold?FontStyleBold:FontStyleRegular,UnitPixel);
            it=cache.emplace(key,f).first;
        }
        return it->second;
    }
    // 文字辅助
    void text(Graphics& g,const std::wstring& s,float x,float y,const Color& c,
              float sz=16,bool bold=false,bool center=false){
        if(s.empty())return;
        Font* f=cachedFont(sz,bold);
        SolidBrush br(c);
        StringFormat fmt;
        PointF p{x,y};
        if(center){
            RectF box(x,y,WIN_W,80);
            fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(s.c_str(),(INT)s.size(),f,box,&fmt,&br);
        }else{
            fmt.SetAlignment(StringAlignmentNear); fmt.SetLineAlignment(StringAlignmentNear);
            g.DrawString(s.c_str(),(INT)s.size(),f,p,&fmt,&br);
        }
    }
    void textC(Graphics& g,const std::wstring& s,float cx,float cy,const Color& c,
               float sz=16,bool bold=false,const wchar_t* face=nullptr){
        const wchar_t* fn = face ? face : m_fontName.c_str();
        RectF box(cx-WIN_W/2.f,cy-40,WIN_W,80);
        Font* f = face ? new Font(fn,sz,bold?FontStyleBold:FontStyleRegular,UnitPixel)
                       : cachedFont(sz,bold);
        SolidBrush br(c);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(s.c_str(),(INT)s.size(),f,box,&fmt,&br);
        if(face) delete f;
    }
    // 波浪文字: 逐字符彩色渐变 + 正弦上下起伏 (speed 起伏速度, amp 起伏幅度)
    void waveText(Graphics& g,const std::wstring& s,float cx,float cy,
                  float sz,bool bold,float speed=0.007f,float amp=4.f){
        float charW=sz*0.62f;
        float totalW=(float)s.size()*charW;
        float x0=cx-totalW/2.f;
        ULONGLONG tt=GetTickCount64();
        for(size_t i=0;i<s.size();++i){
            wchar_t ch[2]={s[i],0};
            float hue=(float)((tt/6+i*45)%3600)/10.f;       // 逐字符色相渐变
            float wave=std::sin(tt*speed+i*1.1f)*amp;       // 逐字符正弦波动
            Color c=hsvColor(hue,0.85f,1.0f);
            RectF box(x0+i*charW-charW,cy-50+wave,charW*2,100);
            Font f(L"Times New Roman",sz,bold?FontStyleBold:FontStyleRegular,UnitPixel);
            SolidBrush br(c);
            StringFormat fmt;
            fmt.SetAlignment(StringAlignmentCenter);fmt.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(ch,1,&f,box,&fmt,&br);
        }
    }
    // 圆
    void circle(Graphics& g,PointF c,float r,const Color& fill,
                const Color& outline={0,0,0,0},float penW=1.f){
        SolidBrush br(fill);
        g.FillEllipse(&br,c.X-r,c.Y-r,r*2,r*2);
        if(outline.GetA()>0){
            Pen pn(outline,penW);
            g.DrawEllipse(&pn,c.X-r,c.Y-r,r*2,r*2);
        }
    }
    // 粗线
    void line(Graphics& g,PointF a,PointF b,const Color& c,float w){
        Pen pn(c,w);
        pn.SetStartCap(LineCapRound); pn.SetEndCap(LineCapRound);
        g.DrawLine(&pn,a,b);
    }
    // 圆角面板
    void panel(Graphics& g,float x,float y,float w,float h,const Color& bg,
               const Color& border={0,0,0,0},float bw=1.f){
        SolidBrush br(bg);
        g.FillRectangle(&br,x,y,w,h);
        if(border.GetA()>0){
            Pen pn(border,bw);
            g.DrawRectangle(&pn,x,y,w,h);
        }
    }

    // ===== 菜单 =====
    void drawMenu(Graphics& g){
        // 背景装饰
        SolidBrush hb(Color(255,236,240,248));
        g.FillRectangle(&hb,0,0,FULL_W,280);
        LinearGradientBrush grad(PointF(0,0),PointF(FULL_W,0),
            Color(255,70,80,140),Color(255,180,60,70));
        g.FillRectangle(&grad,0,0,FULL_W,6);

        textC(g,L"Binary Tree Battle V6.3.0",FULL_W/2.f,170,Color(255,25,25,30),40,true);
        textC(g,L"ITERATIVE AI  SELF-LEARNING ENGINE",FULL_W/2.f,215,Color(255,120,120,130),15);

        // 装饰线
        Pen gp(CLR_GOLD,2);
        g.DrawLine(&gp,FULL_W/2.f-100.f,240.f,FULL_W/2.f+100.f,240.f);

        // 模式选项
        auto opt=[&](const wchar_t* s,float y,bool sel){
            RectF box(FULL_W/2.f-220,y-18,440,36);
            if(sel){
                SolidBrush bg(sel?Color(255,230,235,250):Color(255,250,250,250));
                g.FillRectangle(&bg,box);
                Pen pn(Color(255,80,110,190),2);
                g.DrawRectangle(&pn,box);
            }
            textC(g,s,FULL_W/2.f,y,sel?Color(255,20,20,25):Color(255,140,140,148),
                  sel?19.f:16.f,sel);
            if(sel){ textC(g,L"▶",FULL_W/2.f-235,y,Color(255,20,20,25),20,true); }
        };
        if(m_menuPhase==0){
            // ===== 模式选择 =====
            opt(m_menuSel==0?L"PvP Mode (Two Players)":L"  PvP Mode (Two Players)",430,m_menuSel==0);
            opt(m_menuSel==1?L"vs AI (You = Blue, AI = Red)":L"  vs AI (You = Blue, AI = Red)",474,m_menuSel==1);
            opt(m_menuSel==2?L"AI Battle (Custom AI vs Custom AI)":L"  AI Battle (Custom AI vs Custom AI)",518,m_menuSel==2);
            opt(m_menuSel==3?L"Replay (.btb)":L"  Replay (.btb)",562,m_menuSel==3);
            opt(m_menuSel==4?L"Settings":L"  Settings",606,m_menuSel==4);
        }else if(m_menuPhase==8){   // ---- 设置页 ----
            textC(g,L"SETTINGS",FULL_W/2.f,388,Color(255,90,110,140),20,true);
            // 地图尺寸 (Enter 进入二级菜单)
            {
                std::wstring s = L"Map Size:  ";
                s += kMapName[m_settings.mapIdx];
                s += L"    [Enter]";
                textC(g,s.c_str(),FULL_W/2.f,424, m_menuSel==0?Color(255,20,20,25):Color(255,100,100,110),
                      m_menuSel==0?18.f:16.f, m_menuSel==0);
            }
            // 保存对局 (Enter 进入二级菜单, 分模式)
            {
                std::wstring s = L"Save Replays:    [Enter]";
                textC(g,s.c_str(),FULL_W/2.f,456, m_menuSel==1?Color(255,20,20,25):Color(255,100,100,110),
                      m_menuSel==1?18.f:16.f, m_menuSel==1);
            }
            // 游戏规则 (Enter 进入二级菜单, 数值)
            {
                std::wstring s = L"Game Rules:    [Enter]";
                textC(g,s.c_str(),FULL_W/2.f,488, m_menuSel==2?Color(255,20,20,25):Color(255,100,100,110),
                      m_menuSel==2?18.f:16.f, m_menuSel==2);
            }
            // 快捷键开关 (仅 Enter 切换; ON 绿色 / OFF 暗色)
            struct{ const wchar_t* name; bool* val; } rows[] = {
                {L"B      -  Snap", &m_settings.snapEnabled},
                {L"Ctrl+Z -  Undo", &m_settings.undoEnabled},
                {L"R      -  Depth Display", &m_settings.depthEnabled},
            };
            float py=518;
            for(int i=0;i<3;++i,py+=30){
                bool sel = (m_menuSel==i+3);
                bool on = *rows[i].val;
                std::wstring s = rows[i].name;
                s += on ? L"   [ON]" : L"   [OFF]";
                Color c = on ? Color(255,40,190,70)
                             : (sel ? Color(255,20,20,25) : Color(255,110,110,120));
                textC(g,s.c_str(),FULL_W/2.f,py,c, sel?17.f:15.f, sel||on);
            }
            // 虚拟组网 (Enter 进入二级菜单)
            {
                std::wstring s = L"Virtual Network (EasyTier/VPN)    [Enter]";
                Color c = m_settings.vpnEnable ? Color(255,40,190,70) : Color(255,110,110,120);
                textC(g,s.c_str(),FULL_W/2.f,610, m_menuSel==6?Color(255,20,20,25):c,
                      m_menuSel==6?18.f:16.f, m_menuSel==6||m_settings.vpnEnable);
            }
            textC(g,L"Enter: select/toggle    Esc: back",FULL_W/2.f,646,Color(255,150,150,158),13);
        }else if(m_menuPhase==13){  // ---- 虚拟组网 (EasyTier/VPN) 二级菜单 ----
            textC(g,L"VIRTUAL NETWORK (EasyTier/VPN)",FULL_W/2.f,388,Color(255,90,110,140),18,true);
            const wchar_t* names[6]={
                L"Enable Virtual Network",
                L"Network Name",
                L"Network Secret",
                L"Local Virtual IP",
                L"Custom Command",
                L"Launch EasyTier Now",
            };
            std::wstring vals[6];
            vals[0] = m_settings.vpnEnable ? L"[ON]" : L"[OFF]";
            vals[1] = m_settings.vpnName.empty()?  L"(not set)" : std::wstring(m_settings.vpnName.begin(),m_settings.vpnName.end());
            vals[2] = m_settings.vpnSecret.empty()?L"(not set)" : std::wstring(m_settings.vpnSecret.begin(),m_settings.vpnSecret.end());
            vals[3] = m_settings.vpnIP.empty()?    L"(not set)" : std::wstring(m_settings.vpnIP.begin(),m_settings.vpnIP.end());
            vals[4] = m_settings.vpnCmd.empty()?   L"(default easytier-core)" : std::wstring(m_settings.vpnCmd.begin(),m_settings.vpnCmd.end());
            vals[5] = L"";
            float py=424;
            for(int i=0;i<6;++i,py+=38){
                bool sel=(m_menuSel==i);
                std::wstring s=names[i];
                if(i==0) s += L"   "+vals[0];
                else if(i<5) s += L":  "+vals[i];
                Color c = (i==0&&m_settings.vpnEnable) ? Color(255,40,190,70)
                       : (sel?Color(255,20,20,25):Color(255,110,110,120));
                textC(g,s.c_str(),FULL_W/2.f,py,c, sel?18.f:16.f, sel||(i==0&&m_settings.vpnEnable));
            }
            textC(g,L"Enter: edit/toggle    Esc: back",FULL_W/2.f,652,Color(255,150,150,158),13);
        }else if(m_menuPhase==11){  // ---- 游戏规则二级菜单 (数值) ----
            textC(g,L"GAME RULES",FULL_W/2.f,392,Color(255,90,110,140),20,true);
            struct{ const wchar_t* name; int* v; } rows[] = {
                {L"Extend Max Level", &m_settings.extendMax},
                {L"Extend Cost (pts/level)", &m_settings.extendCost},
                {L"Reinforce Cost (pts/level)", &m_settings.reinfCost},
                {L"Score Point Cap", &m_settings.maxScorePts},
                {L"Initial Score Points", &m_settings.initScorePts},
            };
            float py=448;
            for(int i=0;i<5;++i,py+=40){
                bool sel=(m_menuSel==i);
                std::wstring s=rows[i].name;
                s += L"  :  " + std::to_wstring(*rows[i].v);
                textC(g,s.c_str(),FULL_W/2.f,py, sel?Color(255,20,20,25):Color(255,110,110,120),
                      sel?18.f:16.f, sel);
            }
            textC(g,L"←/→ adjust    Esc: back",FULL_W/2.f,640,Color(255,150,150,158),13);
        }else if(m_menuPhase==9){   // ---- 分辨率二级菜单 ----
            textC(g,L"MAP SIZE",FULL_W/2.f,392,Color(255,90,110,140),20,true);
            float py=440;
            for(int i=0;i<5;++i,py+=42){
                std::wstring s = kMapName[i];
                if(i==m_settings.mapIdx) s += L"   ★";
                std::wstring disp = (m_menuSel==i)?s:(L"  "+s);
                opt(disp.c_str(), py, m_menuSel==i);
            }
            textC(g,L"Enter: select    Esc: back",FULL_W/2.f,640,Color(255,150,150,158),13);
        }else if(m_menuPhase==10){  // ---- 保存对局二级菜单 (分模式) ----
            textC(g,L"SAVE REPLAYS",FULL_W/2.f,392,Color(255,90,110,140),20,true);
            struct{ const wchar_t* name; bool* val; } rows[] = {
                {L"PvP       (Two Players)", &m_settings.savePvp},
                {L"vs AI     (You = Blue)", &m_settings.savePva},
                {L"AI Battle (Auto vs Auto)", &m_settings.saveAva},
            };
            float py=452;
            for(int i=0;i<3;++i,py+=44){
                bool sel = (m_menuSel==i);
                bool on = *rows[i].val;
                std::wstring s = rows[i].name;
                s += on ? L"   [ON]" : L"   [OFF]";
                Color c = on ? Color(255,40,190,70)                 // ON 绿色
                             : (sel ? Color(255,20,20,25) : Color(255,110,110,120));
                textC(g,s.c_str(),FULL_W/2.f,py,c, sel?18.f:16.f, sel||on);
            }
            textC(g,L"Enter: toggle    Esc: back",FULL_W/2.f,640,Color(255,150,150,158),13);
        }else if(m_menuPhase==3){   // PvP 子菜单: 本机2人 / 本机4人 / 在线
            textC(g,L"PVP MODE",FULL_W/2.f,412,Color(255,90,110,140),18,true);
            opt(m_menuSel==0?L"Local 2 Players":L"  Local 2 Players",456,m_menuSel==0);
            opt(m_menuSel==1?L"Local 4 Players (4 corners)":L"  Local 4 Players (4 corners)",496,m_menuSel==1);
            opt(m_menuSel==2?L"Online (Room Server)":L"  Online (Room Server)",536,m_menuSel==2);
            textC(g,L"Online: host a room or join via IP:port",FULL_W/2.f,586,
                  Color(255,130,130,140),13);
            textC(g,L"Example: 192.168.1.5:8080   (IPv4 only)",FULL_W/2.f,608,
                  Color(255,150,150,158),12);
        }else if(m_menuPhase==12){  // ---- 在线子菜单: 人数 / 开房 / 加入 ----
            textC(g,L"ONLINE (ROOM SERVER)",FULL_W/2.f,396,Color(255,90,110,140),18,true);
            std::wstring cnt = (m_netRoomSize==4)?L"Players: 4    [←/→]":L"Players: 2    [←/→]";
            opt(m_menuSel==0 ? cnt.c_str() : (L"  "+cnt).c_str(),444,m_menuSel==0);
            opt(m_menuSel==1?L"Host a Room":L"  Host a Room",492,m_menuSel==1);
            opt(m_menuSel==2?L"Join by Room Code":L"  Join by Room Code",540,m_menuSel==2);
            if(m_settings.vpnEnable && !m_settings.vpnIP.empty()){
                std::wstring s=L"VPN: "+std::wstring(m_settings.vpnIP.begin(),m_settings.vpnIP.end());
                textC(g,s.c_str(),FULL_W/2.f,584,Color(255,0,170,90),13,true);
            }
            textC(g,L"Host: enter room-server address, get a room code",FULL_W/2.f,604,
                  Color(255,130,130,140),13);
            textC(g,L"Join: enter 'serverAddr roomCode'  (e.g. 192.168.1.5:8080 1234)",FULL_W/2.f,622,
                  Color(255,150,150,158),12);
        }else if(m_menuPhase==1 || m_menuPhase==4 || m_menuPhase==6){
            // ===== AI 配置文件(dat) 列表: 1=vs AI红方, 4=AI Battle红方, 6=AI Battle蓝方 =====
            const wchar_t* datTitle = (m_menuPhase==4) ? L"RED  AI  —  select AI config (.dat)"
                              : (m_menuPhase==6) ? L"BLUE  AI  —  select AI config (.dat)"
                              : L"SELECT  AI  CONFIG  (.dat)";
            textC(g,datTitle,FULL_W/2.f,392,Color(255,90,110,140),18,true);
            if(m_reasoners.empty()){
                textC(g,L"⚠ 无 AI 配置文件 (ai_*.dat)，无法对战！",FULL_W/2.f,470,Color(255,220,60,60),24,true);
                textC(g,L"请把 ai_*.dat 放到游戏目录后重新进入",FULL_W/2.f,515,Color(255,140,140,148),15);
                textC(g,L"Press ESC to return",FULL_W/2.f,555,Color(255,150,150,158),13);
            }else{
                int cnt=(int)m_reasoners.size();
                const int VIS=6;
                int start = cnt<=VIS ? 0 : std::max(0, std::min(m_menuSel - VIS/2, cnt-VIS));
                float py=440;
                for(int i=start; i<std::min(cnt,start+VIS); ++i, py+=38){
                    std::wstring label; for(char ch:m_reasoners[i]) label+=(wchar_t)ch;
                    std::wstring disp = (m_menuSel==i)?label:(L"  "+label);
                    opt(disp.c_str(), py, m_menuSel==i);
                }
                if(cnt>VIS) textC(g,L"… ↑/↓ to browse",FULL_W/2.f,py+4,Color(255,150,150,158),12);
            }
        }else{   // phases 2,5,7: 难度/插件 (2=vs AI红方, 5=AI Battle红方, 7=AI Battle蓝方)
            const wchar_t* dTitle = (m_menuPhase==5) ? L"RED  AI  —  Difficulty / Plugin"
                              : (m_menuPhase==7) ? L"BLUE  AI  —  Difficulty / Plugin"
                              : L"DIFFICULTY  /  PLUGIN";
            textC(g,dTitle,FULL_W/2.f,392,Color(255,90,110,140),18,true);
            opt(m_menuSel==0?L"EASY  -  AI plays 2nd best":L"  EASY  -  AI plays 2nd best",448,m_menuSel==0);
            opt(m_menuSel==1?L"NORMAL  -  AI plays best":L"  NORMAL  -  AI plays best",478,m_menuSel==1);
            opt(m_menuSel==2?L"HARD  -  Boosted AI":L"  HARD  -  Boosted AI",508,m_menuSel==2);
            int shown=std::min((int)m_plugins.size(),3);
            float py=540;
            for(int i=0;i<shown;++i,py+=32){
                std::wstring label=L"◆ ";
                for(char ch : m_plugins[i].name) label+=(wchar_t)ch;
                if((int)m_plugins[i].name.size()<18 && !m_plugins[i].author.empty()){
                    label+=L"  (by ";
                    for(char ch : m_plugins[i].author) label+=(wchar_t)ch;
                    label+=L")";
                }
                std::wstring disp = (m_menuSel==(3+i)) ? label : (L"  "+label);
                opt(disp.c_str(), py, m_menuSel==(3+i));
            }
            if((int)m_plugins.size()>3)
                textC(g,L"… more plugins in ai_plugins\\",FULL_W/2.f,py+4,Color(255,150,150,158),12);
            if(m_plugins.empty())
                textC(g,L"No AI plugins found in ai_plugins\\",FULL_W/2.f,540,Color(255,150,150,158),12);
        }

        // 闪烁 Enter 提示 (设置页与二级菜单不显示)
        if(m_menuPhase!=8 && m_menuPhase!=9 && m_menuPhase!=10 && m_menuPhase!=13){
            float a=0.5f+0.5f*std::sin(GetTickCount64()*0.003f);
            Color blink(255,(int)(a*255),(int)(180+a*70),0);
            textC(g,L"Press ENTER to Start",FULL_W/2.f,646,blink,20,true);
        }
        text(g,L"Ctrl+R Restart",FULL_W-150.f,WIN_H-26,Color(255,150,150,150),12);
        text(g,L"v6.3.0 Win32+GDI+",10,WIN_H-26,Color(255,150,150,150),12);
    }

    // ===== 对战渲染 =====
    void drawGame(Graphics& g){
        // 背景网格装饰
        Pen gp(Color(40,220,225,235),1);
        for(int x=40;x<WIN_W;x+=40)g.DrawLine(&gp,x,0,x,WIN_H);
        for(int y=40;y<WIN_H;y+=40)g.DrawLine(&gp,0,y,WIN_W,y);
        // 边框
        Pen bp(Color(90,200,205,215),2);
        g.DrawRectangle(&bp,1,1,WIN_W-2,WIN_H-2);

        // 在线等待/选色 (房间服务器: 2~4 人)
        if(m_netWaiting){
            float a=0.6f+0.4f*std::sin(GetTickCount64()*0.003f);
            const wchar_t* cn[4]={L"RED",L"BLUE",L"GREEN",L"YELLOW"};
            std::wstring pcount = L"Players: " + std::to_wstring(m_netRoomSize);
            if(m_netRole==0 && m_netSock==INVALID_SOCKET){
                textC(g,L"Hosting room - waiting for opponent...",WIN_W/2.f,WIN_H/2.f-64,
                      Color(255,(int)(60*a+60),(int)(160*a+60),220),26,true);
                std::wstring srv(m_netServer.begin(),m_netServer.end());
                textC(g,L"Share this address:  "+srv,WIN_W/2.f,WIN_H/2.f-18,Color(255,120,120,130),16,true);
            }else{
                textC(g,L"Connected - pick your color   ("+pcount+L")",WIN_W/2.f,WIN_H/2.f-64,
                      Color(255,(int)(60*a+60),(int)(160*a+60),220),26,true);
            }
            // 颜色选择
            for(int k=0;k<4;++k){
                std::wstring s=L"[";
                s+=(k==m_netColor)?L"■":L"·";
                s+=L"] ";
                s+=cn[k];
                textC(g,s.c_str(),WIN_W/2.f-120.f+k*80.f,WIN_W/2.f+2,
                      k==m_netColor?CLR_TEAMS[k]:Color(255,150,150,155),
                      k==m_netColor?20.f:16.f, k==m_netColor);
            }
            textC(g,L"Press 1-4 to choose color   Esc: back",
                  WIN_W/2.f,WIN_H/2.f+46,Color(255,140,140,148),13);
            if(m_netRoomFull)
                textC(g,L"Room full - waiting for host to start...",WIN_W/2.f,WIN_H/2.f+72,
                      Color(255,170,150,80),12);
            else if(m_netRole==1)
                textC(g,L"Waiting for more players to join...",WIN_W/2.f,WIN_H/2.f+72,
                      Color(255,170,150,80),12);
            return;
        }
        // 在线对手离开
        if(m_netOppLeft){
            textC(g,L"Opponent has left the game",WIN_W/2.f,WIN_H/2.f-20,Color(255,200,50,50),30,true);
            textC(g,L"Press Ctrl+R to return to menu",WIN_W/2.f,WIN_H/2.f+30,Color(255,140,140,148),15);
            return;
        }

        updateHover();
        drawRange(g);
        drawAIHeat(g);
        drawPluginHeat(g);
        drawEdges(g);
        drawEdgeInfo(g);
        drawScorePts(g);
        drawNodes(g);
        drawDrag(g);
        drawHUD(g);
        // 节点操作面板 (右键节点): 2删除
        if(m_nodeMenu && !m_nodeMenu->removed){
            float pw=320,ph=64,px=(WIN_W-pw)*.5f,py=WIN_H-ph-10;
            panel(g,px,py,pw,ph,Color(220,35,35,40),CLR_GOLD,1.5f);
            textC(g,L"Node Menu  (right-click node)",px+pw/2,py+8,Color(255,255,255,255),14,true);
            textC(g,L"[2] Delete    [Esc] Cancel",
                  px+pw/2,py+32,Color(255,220,220,220),12);
        }
    }

    // 围棋式 AI 选点权重热力图
    void drawAIHeat(Graphics& g){
        // 仅在 AI 思考中(未走下一步前)显示实时选点
        if(!m_aiThinking || !m_thinkingAI) return;
        auto& cands=m_thinkingAI->heatmap();
        if(cands.empty()) return;
        float mn=1e9f,mx=-1e9f;
        for(auto&c:cands){mn=std::min(mn,c.second);mx=std::max(mx,c.second);}
        float range=mx-mn; if(range<0.001f)range=1.f;
        // 蓝(低)→黄(中)→红(高)
        auto heat=[&](float t)->Color{
            t=std::max(0.f,std::min(1.f,t));
            if(t<0.5f){float u=t*2.f; return Color(200,(BYTE)(60+u*120),(BYTE)(150+u*80),(BYTE)(255-u*200));}
            float u=(t-0.5f)*2.f; return Color(210,(BYTE)(180+u*75),(BYTE)(230-u*140),(BYTE)(55-u*55));
        };
        // 标题 + 思考进度
        int pct=(int)(m_thinkingAI->thinkProgress()*100.f);
        textC(g, L"AI thinking... " + std::to_wstring(pct) + L"%",
              WIN_W/2.f, 14, Color(255,120,120,130), 13, true);
        for(auto&c:cands){
            float t=(c.second-mn)/range;
            float rad=7+t*12;
            Color col=heat(t);
            SolidBrush br(Color(150,col.GetR(),col.GetG(),col.GetB()));
            g.FillEllipse(&br,c.first.X-rad,c.first.Y-rad,rad*2,rad*2);
            Pen pn(Color(220,col.GetR(),col.GetG(),col.GetB()),1.5f);
            g.DrawEllipse(&pn,c.first.X-rad,c.first.Y-rad,rad*2,rad*2);
            // 概率百分比
            int pct2=(int)(t*100.f);
            if(pct2>=20){ // 只标较显著的
                std::wstring s=std::to_wstring(pct2)+L"%";
                textC(g,s,c.first.X,c.first.Y-3,Color(255,30,30,30),10,true);
            }
        }
    }

    void updateHover(){
        m_hover=nullptr;
        for(auto*n:m_all)if(len2(n->pos,m_mouse)<NODE_R*2){m_hover=n;break;}
        m_reinfHover=findEdgeAt(m_mouse);
        // 任意边悬停 (不分队伍, 用于显示强度)
        m_hoverEdge=nullptr;
        float bestD=12.f;
        for(auto*n:m_all)
            for(auto&c:n->children){
                float d=ptSegDist(m_mouse,n->pos,c->pos);
                if(d<bestD){bestD=d;m_hoverEdge=c.get();}
            }
    }
    // 悬停边时显示强度标签
    void drawEdgeInfo(Graphics& g){
        if(!m_hoverEdge||!m_hoverEdge->parent) return;
        Node* ch=m_hoverEdge;
        PointF mid{(ch->parent->pos.X+ch->pos.X)*0.5f,
                   (ch->parent->pos.Y+ch->pos.Y)*0.5f};
        PointF d{ch->pos.X-ch->parent->pos.X, ch->pos.Y-ch->parent->pos.Y};
        float L=std::sqrt(d.X*d.X+d.Y*d.Y);
        if(L<1)return;
        PointF u{d.X/L,d.Y/L};
        PointF lp{mid.X-u.Y*18.f, mid.Y+u.X*18.f};   // 垂直偏移到边侧
        std::wstring txt=L"Str: "+std::to_wstring(ch->edgeStrength);
        float pw=74.f, ph=20.f;
        panel(g,lp.X-pw/2,lp.Y-ph/2,pw,ph,Color(235,30,30,35),Color(200,200,210,220),1.f);
        textC(g,txt,lp.X,lp.Y,ch->team,12,true);
    }
    void drawRange(Graphics& g){
        if(m_didBranch)return;
        Node*a=m_sel?m_sel:m_hover;
        if(!a||!teamEq(a->team,m_turn)||a->children.size()>=2)return;
        float r=MAX_D+m_extend*EXTRA_D;
        if(m_sel){
            // 绿色可放置区
            SolidBrush fill(Color(45,60,190,120));
            g.FillEllipse(&fill,a->pos.X-r,a->pos.Y-r,r*2,r*2);
            // 红色禁区
            for(auto*n:m_all){
                if(n==m_sel)continue;
                SolidBrush no(Color(50,230,70,70));
                g.FillEllipse(&no,n->pos.X-OCCUPY_R,n->pos.Y-OCCUPY_R,OCCUPY_R*2,OCCUPY_R*2);
            }
        }
        Color rc{200,100,100,100};
        if(m_extend==1)rc={200,60,190,120};
        else if(m_extend==2)rc={210,255,180,0};
        else if(m_extend==3)rc={230,255,60,60};
        Pen pn(rc,m_extend>0?2.5f:1.5f);
        g.DrawEllipse(&pn,a->pos.X-r,a->pos.Y-r,r*2,r*2);
    }
    // 画正多边形 (2=三角形 ... 6=正六边形), 尖朝上
    void drawPoly(Graphics& g, PointF c, float r, int sides, const Color& fill, const Color& edge){
        std::vector<PointF> pts;
        pts.reserve(sides);
        for(int i=0;i<sides;++i){
            float a = -1.5708f + 6.28318f*i/sides;   // 尖朝上
            pts.push_back({c.X + r*std::cos(a), c.Y + r*std::sin(a)});
        }
        SolidBrush br(fill);
        g.FillPolygon(&br, pts.data(), (INT)pts.size());
        Pen pn(edge, 1.5f);
        g.DrawPolygon(&pn, pts.data(), (INT)pts.size());
    }
    void drawEdges(Graphics& g){
        for(auto*n:m_all){
            for(auto&c:n->children){
                bool isR=(c.get()==m_reinf),isH=(c.get()==m_reinfHover&&!m_reinf);
                // 线段强度 → 线粗细: 强度1~5 → 线宽约2~6 (越粗越强)
                float w = 2.f + (c->edgeStrength - 1) * 1.0f;
                if(isR||isH){
                    Color glow=isR?Color(120,255,215,0):Color(60,255,255,255);
                    line(g,n->pos,c->pos,glow,isR?w+2.f:w+1.f);
                }
                line(g,n->pos,c->pos,n->team,w);
            }
        }
    }
    // 计算节点深度 (根=0, 孤立节点及其子树从0算)
    void computeDepth(Node* n, int d, std::map<Node*,int>& depth, std::set<Node*>& visited){
        if(!n||visited.count(n))return;
        visited.insert(n); depth[n]=d;
        for(auto&c:n->children) computeDepth(c.get(), d+1, depth, visited);
    }
    void drawNodes(Graphics& g){
        // R键: 显示节点深度数字 (根=0, 向外+1)
        if(m_showDepth){
            std::map<Node*,int> depth;
            std::set<Node*> visited;
            if(m_rRoot) computeDepth(m_rRoot.get(),0,depth,visited);
            if(m_bRoot) computeDepth(m_bRoot.get(),0,depth,visited);
            for(auto* n:m_all)
                if(depth.count(n)){
                    std::wstring ds=std::to_wstring(depth[n]);
                    textC(g,ds,n->pos.X,n->pos.Y-16,Color(255,20,20,20),11,true);
                }
        }
        for(auto*n:m_all){
            // 阴影
            circle(g,{n->pos.X+2,n->pos.Y+2},NODE_R,Color(40,0,0,0));
            // 主体: 统一圆形 (节点加强机制已移除)
            Color clr=n->team;
            bool isRoot=(n==m_rRoot.get()||n==m_bRoot.get());
            bool expandable=(teamEq(n->team,m_turn)&&n->children.size()<2&&!m_didBranch);
            circle(g,n->pos,NODE_R,clr);
            if(isRoot){
                Pen pn(CLR_GOLD,3);
                g.DrawEllipse(&pn,n->pos.X-NODE_R-2,n->pos.Y-NODE_R-2,(NODE_R+2)*2,(NODE_R+2)*2);
            }else if(expandable){
                Pen pn(Color(255,255,255,255),2);
                g.DrawEllipse(&pn,n->pos.X-NODE_R-1,n->pos.Y-NODE_R-1,(NODE_R+1)*2,(NODE_R+1)*2);
            }
            if(n==m_sel){
                Pen pn(CLR_GOLD,2);
                g.DrawEllipse(&pn,n->pos.X-NODE_R-6,n->pos.Y-NODE_R-6,(NODE_R+6)*2,(NODE_R+6)*2);
            }
        }
    }
    void drawDrag(Graphics& g){
        if(!m_sel)return;
        PointF m=snapPos(m_mouse);  // 吸附后的预览位置
        line(g,m_sel->pos,m,Color(140,120,120,120),1.5f);
        float d=len2(m_sel->pos,m);
        float maxD=MAX_D+m_extend*EXTRA_D;
        SecureInt& sc=scoreOf(m_turn);
        int cost=m_extend*m_settings.extendCost+(m_str-DEF_S)*m_settings.reinfCost;
        bool valid=true;
        if(d<20||d>maxD||sc<cost)valid=false;
        else{for(auto*n:m_all)if(n!=m_sel&&len2(n->pos,m)<OCCUPY_R){valid=false;break;}}
        if(m.X<10||m.X>WIN_W-10||m.Y<10||m.Y>WIN_H-10)valid=false;
        circle(g,m,NODE_R,valid?Color(220,40,220,60):Color(200,230,60,60),
               valid?Color(255,0,150,0):Color(255,180,0,0),2);

        // ===== 攻击预览 (鼠标位置未变时复用缓存, 性能优化) =====
        AttackPreview ap;
        if(len2(m_sel->pos,m_prevDragFrom)<0.5f && len2(m,m_prevDragTo)<0.5f){
            ap=m_cachedPreview;
        }else{
            ap=previewAttack(m_sel->pos,m,m_turn);
            m_prevDragFrom=m_sel->pos; m_prevDragTo=m; m_cachedPreview=ap;
        }
        float pulse=0.5f+0.5f*std::sin(GetTickCount64()*0.008);
        // 高亮将被摧毁的边 (红粗线)
        for(Node* ec : ap.edgesToKill){
            if(!ec->parent) continue;
            line(g,ec->parent->pos,ec->pos,Color(230,255,40,40),3.5f);
            line(g,ec->parent->pos,ec->pos,Color(160,255,200,80),2.f);
        }
        // 高亮命中的敌方节点 (红圈闪烁)
        for(Node* hn : ap.hitNodes){
            float rr=NODE_R+5+pulse*4;
            circle(g,hn->pos,rr,Color(180,255,50,50),Color(255,255,0,0),2.5f);
        }
        // 右上角面板
        if(ap.nodesHit>0||ap.edgesKilled>0||ap.edgesWeakened>0||ap.hitRoot){
            float pw=180,ph=96,px=WIN_W-pw-10,py=8;
            panel(g,px,py,pw,ph,Color(240,50,20,30),Color(255,230,60,60),1.5f);
            text(g,L"⚔ Attack Preview",px+8,py+5,Color(255,240,60,60),14,true);
            text(g,L"Nodes hit: "+std::to_wstring(ap.nodesHit),px+10,py+26,Color(255,240,240,245),13);
            text(g,L"Edges cut: "+std::to_wstring(ap.edgesKilled),px+10,py+46,Color(255,240,240,245),13);
            text(g,L"Edges weak: "+std::to_wstring(ap.edgesWeakened),px+10,py+66,Color(255,200,200,210),12);
            if(ap.hitRoot){
                panel(g,px,py+ph-24,pw,24,Color(255,220,40,40));
                text(g,L"ROOT REACHED!",px+8,py+ph-20,Color(255,255,255,255),13,true);
            }
        }
    }
    void drawScorePts(Graphics& g){
        // 脉动
        float phase=0.5f+0.5f*std::sin(GetTickCount64()*0.004);
        for(auto&s:m_scores){
            if(!s.alive)continue;
            float pr=SP_R+3+phase*2;
            SolidBrush glow(Color(80,255,215,0));
            g.FillEllipse(&glow,s.pos.X-pr,s.pos.Y-pr,pr*2,pr*2);
            circle(g,s.pos,SP_R,CLR_GOLD,CLR_DGOLD,1.5f);
            std::wstring v=std::to_wstring(s.value);
            textC(g,v,s.pos.X,s.pos.Y-2,Color(255,30,30,30),12,true);
        }
        // 回放模式: 已收集的黄点画灰色标记 (实时追踪收集历史)
        if(m_state==State::Replay){
            const Color doneCol[4]={Color(200,200,80,90),Color(200,60,140,220),Color(200,46,150,113),Color(200,210,160,40)};
            Color doneGray(180,150,150,150);
            for(size_t i=0;i<m_repScores.size();++i){
                auto&sp=m_repScores[i];
                bool alive=false;
                for(auto&s:m_scores)
                    if(s.alive && fabs(s.pos.X-sp.x)<1.f && fabs(s.pos.Y-sp.y)<1.f){ alive=true; break; }
                if(alive) continue;
                // 已收集 → 灰色圆点 + 归属色边
                circle(g,{sp.x,sp.y},SP_R,doneGray,doneGray,1.5f);
                int owner=-1;   // 0..3 (从统计推断: 该位置被收集时归属)
                for(size_t k=0;k<m_repGhostCollected.size();++k)
                    if(len2(m_repGhostCollected[k],{sp.x,sp.y})<5.f){ owner=m_repGhostOwners[k]; break; }
                Color ring = (owner>=0&&owner<4) ? doneCol[owner] : Color(180,120,120,120);
                Pen pn(ring,1.5f);
                g.DrawEllipse(&pn,sp.x-SP_R-2,sp.y-SP_R-2,(SP_R+2)*2,(SP_R+2)*2);
            }
            // 旧文件无 [S] 数据: 幽灵黄点 (推断位置)
            for(size_t i=0;i<m_repGhostCollected.size();++i){
                bool dup=false;
                for(auto&sp:m_repScores)
                    if(len2(m_repGhostCollected[i],{sp.x,sp.y})<5.f){ dup=true; break; }
                if(dup) continue;
                circle(g,m_repGhostCollected[i],SP_R,doneGray,doneGray,1.5f);
                int o=m_repGhostOwners[i];
                Pen pn((o>=0&&o<4)?doneCol[o]:Color(180,120,120,120),1.5f);
                g.DrawEllipse(&pn,m_repGhostCollected[i].X-SP_R-2,m_repGhostCollected[i].Y-SP_R-2,(SP_R+2)*2,(SP_R+2)*2);
            }
        }
    }
    void drawHUD(Graphics& g){
        // 顶/底渐变装饰条 (地图区)
        LinearGradientBrush top(PointF(0,0),PointF(WIN_W,0),
            Color(120,250,250,255),Color(120,240,245,252));
        g.FillRectangle(&top,0,0,WIN_W,50);
        LinearGradientBrush bot(PointF(0,0),PointF(WIN_W,0),
            Color(120,240,245,252),Color(120,250,250,255));
        g.FillRectangle(&bot,0,WIN_H-50,WIN_W,50);

        // ===== 右侧信息面板: 蓝白渐变 =====
        // 右侧面板: 白底 + 上红下蓝渐变
        SolidBrush whiteBg(Color(255,250,252,255));
        g.FillRectangle(&whiteBg,(float)WIN_W,0.f,(float)PANEL_W,(float)WIN_H);
        LinearGradientBrush redZone(PointF(WIN_W,0),PointF(WIN_W,WIN_H*0.4f),
            Color(255,250,225,230),Color(255,255,255,255));   // 淡红→白 (垂直)
        g.FillRectangle(&redZone,(float)WIN_W,0.f,(float)PANEL_W,WIN_H*0.4f);
        LinearGradientBrush blueZone(PointF(WIN_W,WIN_H*0.6f),PointF(WIN_W,WIN_H),
            Color(255,255,255,255),Color(255,215,235,250));   // 白→淡蓝 (垂直)
        g.FillRectangle(&blueZone,(float)WIN_W,WIN_H*0.6f,(float)PANEL_W,WIN_H*0.4f);
        Pen sep(Color(140,175,200,225),1.5f);
        g.DrawLine(&sep,WIN_W,0,WIN_W,WIN_H);

        // 阵营节点计数
        int cnt[4]={0,0,0,0};
        for(auto*n:m_all) cnt[idxOfTeam(n->team)]++;

        // ===== 右侧面板: 按阵营数分区块渲染 (2人=上红下蓝, 4人=四象限) =====
        float pxc=WIN_W+PANEL_W/2.f;
        const wchar_t* cn[4]={L"Red",L"Blue",L"Green",L"Yellow"};
        bool humanTurn = !m_bothAI && !(m_aiMode && idxOfTeam(m_turn)==0);
        auto teamLabel=[&](int ti)->std::wstring{
            // 在线 P2P
            if(m_netActive){
                int owner=-1;
                for(int j=0;j<m_players;++j)
                    if(m_netPlayerColors[j]==ti){ owner=j; break; }
                if(owner<0) return L"";
                std::wstring s=cn[ti];
                if(ti==m_netColor) s+=L" (You)";
                else s+=L" (P"+std::to_wstring(owner)+L")";
                return s;
            }
            // AI / 插件
            int pidx = (ti==0)?m_redPlugin:m_bluePlugin;
            bool plugin = pidx>=0 && pidx<(int)m_plugins.size() && m_plugins[pidx].loaded;
            bool autoSide = m_bothAI || (m_aiMode && ti==0);
            if(autoSide){
                if(plugin){ std::wstring s=L"[Plugin] "; for(char ch:m_plugins[pidx].name) s+=(wchar_t)ch; return s; }
                return (ti==0)?L"AI":L"AI Blue";
            }
            return cn[ti];
        };
        // 区块布局: 2人上(红)/下(蓝); 4人 上左红/上右绿/下左蓝/下右黄
        struct Box{ float y0,y1; float cx; };
        Box box[4];
        float p0=WIN_W+PANEL_W*0.25f, p1=WIN_W+PANEL_W*0.75f;
        float wh=(float)WIN_H;
        if(m_players<=2){
            box[0]={0.f,wh*0.5f,pxc};
            box[1]={wh*0.5f,wh,pxc};
        }else{
            box[0]={0.f,wh*0.5f,p0}; box[2]={0.f,wh*0.5f,p1};
            box[1]={wh*0.5f,wh,p0}; box[3]={wh*0.5f,wh,p1};
        }
        for(int i=0;i<m_players;++i){
            float cx=box[i].cx, y0=box[i].y0, y1=box[i].y1;
            Color tc=CLR_TEAMS[i];
            bool isTurn=teamEq(m_turn,CLR_TEAMS[i]);
            float labY=y0+38.f, scoreY=y0+(y1-y0)*0.4f, nodeY=y0+(y1-y0)*0.62f, statY=y0+(y1-y0)*0.8f;
            // 阵营名 (在线/AI/插件适配)
            std::wstring lbl=teamLabel(i);
            bool pidxPlug = (i==0)?(m_redPlugin>=0&&m_redPlugin<(int)m_plugins.size()&&m_plugins[m_redPlugin].loaded)
                                  :(m_bluePlugin>=0&&m_bluePlugin<(int)m_plugins.size()&&m_plugins[m_bluePlugin].loaded);
            if((m_bothAI||m_aiMode) && i==0 && !pidxPlug)
                waveText(g,L"AI",cx,labY+8,52,true,0.003f,9.f);
            else
                textC(g,lbl.c_str(),cx,labY,tc,pidxPlug?20.f:26.f,true,L"Times New Roman");
            if(isTurn){ // 回合箭头
                SolidBrush br(Color(255,40,40,40));
                PointF pts[3]={{cx-55,labY+10},{cx-65,labY},{cx-45,labY}};
                g.FillPolygon(&br,pts,3);
            }
            textC(g,L"Score: "+std::to_wstring((int)m_plyScores[i]),cx,scoreY,tc,22,true,L"Times New Roman");
            textC(g,L"Nodes: "+std::to_wstring(cnt[i]),cx,nodeY,tc,18,false,L"Times New Roman");
            if(isTurn && humanTurn){ // "Your turn" (仅人类)
                if(m_didBranch){
                    SecureInt& sc=m_plyScores[i];
                    textC(g,sc>=EXTRA_COST?L"✖ Used | X:3pts":L"✖ Used (need 3)",
                          cx,statY,sc>=EXTRA_COST?Color(255,0,140,0):Color(255,200,100,0),14,true);
                }else{
                    textC(g,L"Your turn ▼",cx,statY,Color(255,60,130,60),14,true);
                }
            }
        }

// 强化面板
        if(m_reinf){
            float pw=360,ph=55,px=(WIN_W-pw)*.5f,py=WIN_H-ph-10;
            panel(g,px,py,pw,ph,Color(220,35,35,40),CLR_GOLD,1.5f);
            SecureInt& sc=scoreOf(m_turn);
            int cost=m_reinfStr-m_reinf->edgeStrength;
            for(int i=0;i<5;++i){
                float bx=px+20+i*28.f;
                int lv=i+1;
                Color bg;
                if(lv==m_reinfStr)bg=(cost>0&&sc>=cost)?Color(255,50,200,50):Color(255,200,200,50);
                else if(lv<m_reinf->edgeStrength)bg=Color(255,60,60,60);
                else if(lv<=m_reinfStr)bg=Color(255,100,100,100);
                else bg=Color(255,40,40,40);
                panel(g,bx,py+5,24,20,bg,Color(150,150,150,150));
                textC(g,std::to_wstring(lv),bx+12,py+15,Color(255,255,255,255),11);
            }
            textC(g,L"Current:"+std::to_wstring(m_reinf->edgeStrength)+L" → Target:"+
                std::to_wstring(m_reinfStr)+L" | Cost:"+std::to_wstring(cost),
                px+170,py+30,Color(255,255,255,255),12);
            textC(g,L"[1]-[5]Set | Enter:Apply | Esc:Cancel",px+170,py+46,Color(255,180,180,180),10);
        }
        // 拖拽面板
        if(m_sel){
            float pw=360,ph=55,px=(WIN_W-pw)*.5f,py=WIN_H-ph-10;
            panel(g,px,py,pw,ph,Color(220,35,35,40),Color(150,150,150,150),1);
            SecureInt& sc=scoreOf(m_turn);
            int cost=m_extend+(m_str-DEF_S);
            textC(g,L"Str:"+std::to_wstring(m_str)+L" | Dist:"+
                std::to_wstring((int)(MAX_D+m_extend*EXTRA_D))+L" | Cost:"+std::to_wstring(cost),
                px+pw/2,py+16,Color(255,255,255,255),13);
            textC(g,L"Wheel:Str | Space:+40 | Esc:Reset | RMB:Cancel",px+pw/2,py+38,
                Color(255,180,180,180),11);
        }
        // 边界吸附状态指示
        if(!m_sel&&!m_reinf){
            text(g, m_snapEnabled ? L"Snap:ON  [B]" : L"Snap:OFF  [B]",
                 12, WIN_H-22, m_snapEnabled?Color(255,0,140,60):Color(255,160,160,165),
                 12, m_snapEnabled);
        }
    }
    void drawOverlay(Graphics& g){
        float el=m_restartClock.GetElapsedTime();
        if(el<0.5f)return;
        SolidBrush bg(Color(170,0,0,0));
        g.FillRectangle(&bg,0,0,WIN_W,WIN_H);
        textC(g,m_winner+L" WINS!",WIN_W/2.f,WIN_H/2.f-30,CLR_GOLD,46,true);
        if(el>2.f){
            textC(g,L"Press R to Menu   (Replay auto-saved)",WIN_W/2.f,WIN_H/2.f+30,Color(255,200,200,200),20);
        }
    }

    // ===== 状态 =====
    HINSTANCE m_hInst=nullptr;
    HWND m_hwnd=nullptr;
    PointF m_mouse{0,0};
    std::wstring m_fontName=L"Arial";

    State m_state=State::Menu;
    // V6.2.0: 阵营数组化 (2人=前2, 4人=4个). m_rRoot/m_bRoot 等经宏映射到 [0]/[1]
    std::array<std::unique_ptr<Node>,4> m_roots;
    int m_players = 2;                 // 参与阵营数 (PvP 可 2/4)
    std::vector<Node*> m_all;

    Color m_turn=CLR_RED;
    int  idxOfTeam(const Color& c) const {   // 颜色→阵营索引 (0..3)
        for(int i=0;i<4;++i) if(teamEq(c,CLR_TEAMS[i])) return i;
        return 0;
    }
    Node* rootOf(int i){ return (i>=0&&i<m_players&&m_roots[i])?m_roots[i].get():nullptr; }
    Node* myRoot(){ return rootOf(idxOfTeam(m_turn)); }
    SecureInt& scoreOfTeam(int i){ return m_plyScores[i]; }
    void advanceTurn(){                       // 切换到下一个存活阵营
        int n=m_players;
        for(int s=1;s<=n;++s){
            int k=(idxOfTeam(m_turn)+s)%n;
            if(rootOf(k)){ m_turn=CLR_TEAMS[k]; return; }
        }
    }
    Node*m_sel=nullptr,*m_hover=nullptr;
    int m_extend=0,m_str=DEF_S;
    Node*m_reinf=nullptr; int m_reinfStr=0; Node*m_reinfHover=nullptr;
    Node*m_hoverEdge=nullptr;   // 任意边悬停 (显示强度)
    bool m_didBranch=false;
    bool m_xUsedThisTurn=false;     // 本回合是否已用 X 额外行动 (每回合限1次)
    std::unique_ptr<Bitmap> m_buffer;   // 双缓冲缓存 (性能优化)
    int m_bufW=0, m_bufH=0;
    // 拖拽攻击预览缓存 (鼠标位置未变时复用, 避免每帧重算)
    PointF m_prevDragFrom{0,0}, m_prevDragTo{0,0};
    AttackPreview m_cachedPreview;
    SecureInt m_plyScores[4];   // 玩家积分加密存储 (2人用前2, 4人用4)
    std::vector<ReplayAction> m_replay;   // 对局行动记录
    int m_replayTurn = 0;                 // 当前记录回合
    bool m_replaySaved = false;           // 本局已自动保存
    // 回放状态
    std::vector<ReplayAction> m_repActs;  // 回放行动序列
    int m_repIdx = 0;                     // 当前回放索引
    bool m_repPaused = true;              // 回放暂停
    float m_repSpeed = 1.0f;              // 秒/回合 (默认1秒)
    bool m_repDrag = false;               // 进度条拖拽中
    ULONGLONG m_repLastTick = 0;          // 回放节拍
    std::wstring m_repFile;               // 回放文件名
    int m_repRs=10, m_repBs=10, m_repGs=10, m_repYs=10;   // 回放初始分数 (V6.2.0: 4 方)
    int m_repPlayers=2;                    // 回放玩家数
    std::string m_repWinner="red";        // 回放胜者
    std::vector<ReplayScorePt> m_repScores;      // 回放开局黄点布局 ([S] 段)
    ReplayScoreStats m_repStats;                 // 回放黄点实时统计
    std::vector<PointF> m_repGhostCollected;     // 旧文件推断的幽灵黄点位置
    std::vector<int> m_repGhostOwners;           // 幽灵黄点归属 (0红 1蓝)
    std::vector<ScorePoint> m_initScores;        // 开局黄点快照 (写入 btbdt)
    std::vector<ScorePoint> m_scores;

    // ===== 在线对战 (房间服务器转发: 2~4 人) =====
    SOCKET m_netSock = INVALID_SOCKET;
    SOCKET m_netListen= INVALID_SOCKET;   // 预留: 主机监听 socket
    bool m_netActive = false;        // 在线对局中
    int  m_netRole = 0;              // 0=主机(玩家0, 先手) 1=加入
    int  m_netColor = 0;             // 本机所选颜色索引 (0..3)
    int  m_netRoomSize = 2;          // 房间人数 (2 / 4)
    int  m_netPlayerIdx = 0;         // 本机玩家编号 (0=房主)
    int  m_netPlayerColors[4] = {-1,-1,-1,-1};  // 玩家编号 → 颜色索引
    int  m_netJoined = 1;            // 已加入玩家位图 (位0=房主)
    bool m_netRoomFull = false;      // 房间是否已满
    bool m_netMyTurn = false;        // 是否本机回合
    bool m_netWaiting = false;       // 已连接等待配对/开始
    bool m_netGotSeed = false;       // 是否已收到黄点种子
    std::string m_netBuf;            // 收包缓冲
    std::string m_netServer;         // 本机房间地址 (显示用)
    std::string m_netErrMsg;         // 最近一次连接失败原因 (显示用)
    bool m_netOppLeft = false;       // 对手已离开
    bool m_aiExtraPending = false;   // AI 额外行动尝试中 (第二次思考待评估)
    Node* m_nodeMenu = nullptr;        // 右键节点操作面板 (删除)
    bool m_showDepth = false;          // R键: 显示节点深度 (根=0)
    bool m_over=false; std::wstring m_winner;
    std::mt19937 m_rng;
    AI m_aiRed, m_aiBlue;
    std::vector<AIPlugin> m_plugins;   // 已加载的 AI 插件 (ai_plugins\*.dll)
    int  m_redPlugin = -1;             // 红方 AI 插件索引 (-1=内置)
    int  m_bluePlugin = -1;            // 蓝方 AI 插件索引 (-1=内置)
    int  m_redDiff = 1;                // 红方内置难度 0/1/2
    int  m_blueDiff = 1;               // 蓝方内置难度 0/1/2
    std::vector<std::string> m_reasoners;   // 已发现的 ai_reasoner_XXXX.dat 文件名(按编号排序)
    int  m_redReasoner = 0;            // 红方选中的 reasoner 索引
    int  m_blueReasoner = 0;           // 蓝方选中的 reasoner 索引
    bool m_redReasonerLoaded = false;  // 本局红方是否已加载 reasoner (退出时回写)
    bool m_blueReasonerLoaded = false; // 本局蓝方是否已加载 reasoner
    static constexpr ULONGLONG PLUGIN_TURN_MS = 900;  // 插件每回合最小间隔(ms)
    int  m_pluginWaitTeam = -1;        // 正在等待的插件回合方 (-1=无)
    ULONGLONG m_pluginTurnStart = 0;   // 该插件回合开始时刻
    std::vector<AIPluginCand> m_pluginCands;    // 插件上报的候选落点(热力图)
    bool m_pluginCandsValid = false;   // 当前候选是否已从插件拉取
    bool m_aiMode=false;
    bool m_bothAI=false;
    int m_menuSel=0;
    int m_menuPhase=0;   // 0=模式 1/4/6=dat列表 2/5/7=难度插件 3=PvP子菜单
    int m_diff=1;   // AI 难度: 0简单 1普通 2困难
    // 迭代思考状态 (选点动画)
    bool m_aiThinking=false;
    AI* m_thinkingAI=nullptr;
    Color m_thinkingTeam;
    ULONGLONG m_thinkStart=0;
    ULONGLONG m_lastThinkTick=0;  // 思考动画节拍 (200ms = 5fps)
    int m_aiExtrasUsed=0;  // 本回合额外行动次数
    bool m_snapEnabled=true;  // 边界吸附开关 (初始值由设置决定)
    // ===== 设置 (settings.dat) =====
    struct GameSettings {
        int  mapIdx = 0;              // 地图尺寸索引
        bool snapEnabled   = true;    // B 边界吸附 (初始状态)
        bool undoEnabled   = true;    // Ctrl+Z 撤销
        bool depthEnabled  = false;   // R 深度显示 (初始状态)
        bool savePvp = true;          // 本机 PvP 自动保存对局
        bool savePva = true;          // vs AI 自动保存对局
        bool saveAva = true;          // AI Battle 自动保存对局
        // V6.2.0: 游戏规则 (可在设置里自定义)
        int extendMax   = 3;          // 空格扩展等级上限 (0..5)
        int extendCost  = 1;          // 每级扩展消耗的积分 (0..3)
        int reinfCost   = 1;          // 每级分支/边强化消耗的积分 (0..3)
        int maxScorePts = 5;          // 场上得分点上限 (3..10)
        int initScorePts= 3;          // 开局得分点数量 (2..5)
        // V6.3.0: 虚拟组网 (EasyTier 等) 设置
        bool   vpnEnable = false;     // 启用虚拟网卡联机
        std::string vpnName;          // EasyTier 网络名
        std::string vpnSecret;        // EasyTier 网络密钥
        std::string vpnIP;            // 本机虚拟 IP (开房/加入默认地址)
        std::string vpnCmd;           // 自定义启动命令 (空=用默认 easytier-core)
    } m_settings;
    static inline const int kMapW[5] = {1000,1280,1280,1600,1920};
    static inline const int kMapH[5] = {700, 720, 1024, 900, 1080};
    static inline const wchar_t* kMapName[5] = {L"Default 1000x700", L"1280x720", L"1280x1024", L"1600x900", L"1920x1080"};
    void loadSettings(){
        m_settings = GameSettings{};
        FILE* f = fopen("settings.dat","r");
        if(f){
            char line[256];
            while(fgets(line,sizeof line,f)){
                char k[48]={0}; char v[192]={0};
                if(sscanf(line," %47[^= \t\r\n]=%191[^\r\n]",k,v)>=1){
                    // 去值首尾空白
                    char* s=v; while(*s==' '||*s=='\t') ++s;
                    char* e=s+strlen(s); while(e>s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n')) --e; *e=0;
                    if(!strcmp(k,"map_idx")) m_settings.mapIdx=atoi(s);
                    else if(!strcmp(k,"snap"))   m_settings.snapEnabled  =atoi(s)!=0;
                    else if(!strcmp(k,"undo"))   m_settings.undoEnabled  =atoi(s)!=0;
                    else if(!strcmp(k,"depth"))  m_settings.depthEnabled =atoi(s)!=0;
                    else if(!strcmp(k,"save_pvp")) m_settings.savePvp = atoi(s)!=0;
                    else if(!strcmp(k,"save_pva")) m_settings.savePva = atoi(s)!=0;
                    else if(!strcmp(k,"save_ava")) m_settings.saveAva = atoi(s)!=0;
                    else if(!strcmp(k,"extend_max")) m_settings.extendMax = atoi(s);
                    else if(!strcmp(k,"extend_cost")) m_settings.extendCost = atoi(s);
                    else if(!strcmp(k,"reinf_cost")) m_settings.reinfCost = atoi(s);
                    else if(!strcmp(k,"max_score_pts")) m_settings.maxScorePts = atoi(s);
                    else if(!strcmp(k,"init_score_pts")) m_settings.initScorePts = atoi(s);
                    else if(!strcmp(k,"vpn_enable")) m_settings.vpnEnable = atoi(s)!=0;
                    else if(!strcmp(k,"vpn_name"))   m_settings.vpnName = s;
                    else if(!strcmp(k,"vpn_secret")) m_settings.vpnSecret = s;
                    else if(!strcmp(k,"vpn_ip"))     m_settings.vpnIP = s;
                    else if(!strcmp(k,"vpn_cmd"))    m_settings.vpnCmd = s;
                }
            }
            fclose(f);
        }
        if(m_settings.mapIdx<0) m_settings.mapIdx=0;
        if(m_settings.mapIdx>4) m_settings.mapIdx=4;
        applyMapSize();          // 应用到全局地图尺寸
        m_snapEnabled = m_settings.snapEnabled;   // 初始吸附状态
        m_showDepth   = m_settings.depthEnabled;  // 初始深度显示
    }
    void saveSettings(){
        FILE* f = fopen("settings.dat","w");
        if(!f) return;
        fprintf(f,"map_idx=%d\n",m_settings.mapIdx);
        fprintf(f,"snap=%d\n",m_settings.snapEnabled?1:0);
        fprintf(f,"undo=%d\n",m_settings.undoEnabled?1:0);
        fprintf(f,"depth=%d\n",m_settings.depthEnabled?1:0);
        fprintf(f,"save_pvp=%d\n",m_settings.savePvp?1:0);
        fprintf(f,"save_pva=%d\n",m_settings.savePva?1:0);
        fprintf(f,"save_ava=%d\n",m_settings.saveAva?1:0);
        fprintf(f,"extend_max=%d\n",m_settings.extendMax);
        fprintf(f,"extend_cost=%d\n",m_settings.extendCost);
        fprintf(f,"reinf_cost=%d\n",m_settings.reinfCost);
        fprintf(f,"max_score_pts=%d\n",m_settings.maxScorePts);
        fprintf(f,"init_score_pts=%d\n",m_settings.initScorePts);
        fprintf(f,"vpn_enable=%d\n",m_settings.vpnEnable?1:0);
        fprintf(f,"vpn_name=%s\n",m_settings.vpnName.c_str());
        fprintf(f,"vpn_secret=%s\n",m_settings.vpnSecret.c_str());
        fprintf(f,"vpn_ip=%s\n",m_settings.vpnIP.c_str());
        fprintf(f,"vpn_cmd=%s\n",m_settings.vpnCmd.c_str());
        fclose(f);
    }
    // 启动虚拟组网软件 (EasyTier 等): 优先自定义命令, 否则用默认 easytier-core
    void launchVpn(){
        std::string cmd = m_settings.vpnCmd;
        if(cmd.empty())
            cmd = "easytier-core -n " + m_settings.vpnName + " -s " + m_settings.vpnSecret;
        int n=MultiByteToWideChar(CP_UTF8,0,cmd.c_str(),-1,nullptr,0);
        std::wstring wcmd;
        if(n>0){ wcmd.resize(n); MultiByteToWideChar(CP_UTF8,0,cmd.c_str(),-1,&wcmd[0],n); }
        // cmd /c start 新开最小化窗口运行, 不阻塞游戏
        std::wstring full = L"/c start \"EasyTier\" /min " + wcmd;
        HINSTANCE hr = ShellExecuteW(nullptr,L"open",L"cmd.exe",full.c_str(),nullptr,SW_HIDE);
        if((INT_PTR)hr<=32){
            MessageBoxW(m_hwnd,
                L"Failed to launch VPN software.\n\n"
                L"Install EasyTier (easytier-core) or set a custom command in Settings.",
                L"Virtual Network",MB_OK|MB_ICONWARNING);
        }
    }
    void applyMapSize(){        // 更新全局地图尺寸并调整窗口 (居中显示)
        WIN_W = kMapW[m_settings.mapIdx];
        WIN_H = kMapH[m_settings.mapIdx];
        FULL_W = WIN_W + PANEL_W;
        if(m_hwnd){
            RECT rc{0,0,FULL_W,WIN_H};
            AdjustWindowRectEx(&rc,WS_OVERLAPPEDWINDOW,FALSE,0);
            int w=rc.right-rc.left, h=rc.bottom-rc.top;
            // 居中: 基于屏幕工作区, 避免切换大分辨率后窗口偏向屏幕右下
            RECT wa;
            SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0);
            int x = wa.left + ((wa.right-wa.left)-w)/2;
            int y = wa.top + ((wa.bottom-wa.top)-h)/2;
            if(x<wa.left) x=wa.left; if(y<wa.top) y=wa.top;
            SetWindowPos(m_hwnd,nullptr,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
            InvalidateRect(m_hwnd,nullptr,FALSE);
        }
    }
    ULONGLONG m_minThink=2500, m_maxThink=4500;   // 思考时间 (困难难度更长)
    ULONG_PTR m_gdiToken=0;
    // 计时 (用 GetTickCount64)
    ULONGLONG m_enterT=0,m_restartT=0,m_aiT=0;
    struct Clock{
        ULONGLONG last=GetTickCount64();
        void Restart(){last=GetTickCount64();}
        double GetElapsedTime()const{return (double)(GetTickCount64()-last)/1000.0;}
    };
    Clock m_enterClock,m_restartClock,m_aiClock;
};

// ===== 入口 =====
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int){
    if(!Game::I().init(hInst))return 1;
    return Game::I().run();
}
