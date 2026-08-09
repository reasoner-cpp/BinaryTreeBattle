/**
 * btbserver.cpp — Binary Tree Battle 房间转发服务器 (V6.3.0)
 *
 * 客户端主动连服务器（出站，无需开放入站端口）。服务器按"房间码"配对 2~4 个玩家并
 * 在房间内互相转发消息。
 *
 * 协议 (文本行, \n 结尾):
 *   客户端→服务器:
 *     CREATE <n>             开房（n=2 或 4；服务器返回 ROOM <code>）
 *     JOIN <code>            加入房间（服务器返回 JOINED <idx>，并向房内广播 PEER JOINED <idx>）
 *     COLOR <colorIdx>       选色（服务器查重，冲突返回 TAKECOLOR；否则广播 PEER COLOR <idx> <colorIdx>）
 *     BYE                    退出
 *     <任意游戏消息>         由服务器转发给房间内其他所有人（加 "PEER " 前缀）
 *   服务器→客户端:
 *     ROOM <code>            开房成功，房间码
 *     JOINED <idx>           加入成功，idx 为本玩家编号（0=房主）
 *     PEER JOINED <idx>      通知其他玩家有新玩家加入
 *     PEER FULL <n>          房间已满
 *     PEER COLOR <idx> <c>   某玩家选色
 *     TAKECOLOR              颜色冲突，请重选
 *     PEER <msg>             其他玩家转发的消息
 *     PEER BYE               有人退出
 *
 * 用法: btbserver [port=8080]
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <vector>

static std::atomic<bool> g_running{true};
static std::mutex g_mtx;
static std::atomic<int> g_roomSeq{1000};

struct Room {
    int cap = 2;                    // 房间容量 2 / 4
    std::vector<SOCKET> peers;      // 已加入的 socket
    std::vector<int> colors;        // 每位玩家的颜色 (-1=未选)
};
static std::map<int, Room> g_rooms; // code -> room

static bool recvLine(SOCKET s, std::string& pending, std::string& out){
    char tmp[512];
    int n = recv(s, tmp, sizeof tmp, 0);
    if(n <= 0) return false;
    pending.append(tmp, n);
    size_t pos = pending.find('\n');
    if(pos == std::string::npos){
        if(pending.size() > 8192) pending.clear();
        return false;
    }
    out = pending.substr(0, pos);
    pending.erase(0, pos + 1);
    return true;
}
static void sendLine(SOCKET s, const std::string& line){
    if(s==INVALID_SOCKET) return;
    std::string msg = line + "\n";
    send(s, msg.c_str(), (int)msg.size(), 0);
}
static void closeSock(SOCKET s){
    if(s!=INVALID_SOCKET){ shutdown(s,SD_BOTH); closesocket(s); }
}

// 向房间内除 exceptIdx 外的所有 socket 发送（需已持有 g_mtx）
static void broadcast(Room& r, int exceptIdx, const std::string& msg){
    for(size_t i=0;i<r.peers.size();++i)
        if((int)i != exceptIdx && r.peers[i]!=INVALID_SOCKET)
            sendLine(r.peers[i], msg);
}

// 服务一个已加入房间的 socket: 读取消息并广播给房间内其他人
static void servePeer(int code, int idx){
    SOCKET s;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_rooms.find(code);
        if(it==g_rooms.end()) return;
        s = it->second.peers[idx];
    }
    std::string pending, line;
    while(g_running){
        if(!recvLine(s, pending, line)) break;          // 断开
        if(line=="BYE") break;
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_rooms.find(code);
        if(it==g_rooms.end()) break;
        Room& r = it->second;
        if(line.rfind("COLOR ",0)==0){                  // 选色: 服务器查重
            int c = atoi(line.c_str()+6);
            if(c<0 || c>3){ sendLine(s,"TAKECOLOR\n"); continue; }
            bool taken=false;
            for(size_t i=0;i<r.peers.size();++i)
                if((int)i!=idx && r.colors[i]==c){ taken=true; break; }
            if(taken){ sendLine(s,"TAKECOLOR\n"); continue; }
            r.colors[idx]=c;
            char m[40]; snprintf(m,sizeof m,"PEER COLOR %d %d\n", idx, c);
            broadcast(r, idx, m);
            continue;
        }
        broadcast(r, idx, "PEER "+line);
    }
    closeSock(s);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_rooms.find(code);
        if(it==g_rooms.end()) return;
        Room& r = it->second;
        if(idx < (int)r.peers.size()) r.peers[idx]=INVALID_SOCKET;
        broadcast(r, idx, "PEER BYE\n");
        bool any=false;
        for(SOCKET p : r.peers) if(p!=INVALID_SOCKET) any=true;
        if(!any) g_rooms.erase(it);
    }
}

static void openFirewallPort(int port){
    char cmd[512];
    snprintf(cmd, sizeof cmd,
        "netsh advfirewall firewall add rule name=\"BTBServer%d\" dir=in action=allow protocol=TCP localport=%d", port, port);
    wchar_t wcmd[512]; swprintf(wcmd, 512, L"%hs", cmd);
    ShellExecuteW(nullptr, L"runas", L"net.exe", wcmd, nullptr, SW_HIDE);
}

int main(int argc, char** argv){
    SetConsoleOutputCP(CP_UTF8);
    int port = 8080;
    if(argc > 1) port = atoi(argv[1]);
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2,2), &wsa)!=0){ printf("WSAStartup failed\n"); return 1; }
    // IPv4 仅监听
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(ls==INVALID_SOCKET){ printf("socket failed\n"); return 1; }
    int reuse=1; setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof reuse);
    sockaddr_in a4{};
    a4.sin_family=AF_INET; a4.sin_addr.s_addr=INADDR_ANY; a4.sin_port=htons((u_short)port);
    if(bind(ls,(sockaddr*)&a4,sizeof a4)!=0){
        printf("bind 0.0.0.0:%d failed (port in use?)\n", port); return 1;
    }
    listen(ls,SOMAXCONN);
    printf("=== Binary Tree Battle Room Server (IPv4) ===\n");
    printf("Listening: 0.0.0.0:%d\n", port);
    openFirewallPort(port);
    printf("Waiting for players...\n");

    while(g_running){
        sockaddr_in ca{}; int al=sizeof ca;
        SOCKET c = accept(ls,(sockaddr*)&ca,&al);
        if(c==INVALID_SOCKET) continue;
        std::string pending, line;
        if(!recvLine(c, pending, line)){ closeSock(c); continue; }
        if(line.rfind("CREATE ",0)==0){                 // 开房
            int cap = atoi(line.c_str()+7);
            if(cap!=2 && cap!=4) cap=2;
            int code = ++g_roomSeq;
            Room r; r.cap=cap; r.peers.push_back(c); r.colors.push_back(-1);
            { std::lock_guard<std::mutex> lk(g_mtx); g_rooms[code]=std::move(r); }
            char rsp[32]; snprintf(rsp,sizeof rsp,"ROOM %d\n",code);
            sendLine(c, rsp);
            printf("[room %d] created (cap %d)\n", code, cap);
            std::thread(servePeer, code, 0).detach();
        } else if(line.rfind("JOIN ",0)==0){            // 加入
            int code = atoi(line.c_str()+5);
            bool ok=false;
            {
                std::lock_guard<std::mutex> lk(g_mtx);
                auto it=g_rooms.find(code);
                if(it!=g_rooms.end() && it->second.peers.size() < (size_t)it->second.cap){
                    int idx=(int)it->second.peers.size();
                    it->second.peers.push_back(c);
                    it->second.colors.push_back(-1);
                    ok=true;
                    char rsp[32]; snprintf(rsp,sizeof rsp,"JOINED %d\n", idx);
                    sendLine(c, rsp);
                    char m[48]; snprintf(m,sizeof m,"PEER JOINED %d\n", idx);
                    broadcast(it->second, idx, m);
                    if(it->second.peers.size() >= (size_t)it->second.cap){
                        char m2[32]; snprintf(m2,sizeof m2,"PEER FULL %d\n", it->second.cap);
                        broadcast(it->second, -1, m2);
                    }
                    std::thread(servePeer, code, idx).detach();
                    printf("[room %d] player %d joined\n", code, idx);
                }
            }
            if(!ok){ sendLine(c,"PEER BYE\n"); closeSock(c); printf("[join %d] room not found/full\n",code); }
        } else {
            closeSock(c);
        }
    }
    closesocket(ls);
    WSACleanup();
    return 0;
}
