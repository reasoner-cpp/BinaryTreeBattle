/**
 * btbserver.cpp — Binary Tree Battle 房间转发服务器 (V6.2.0)
 *
 * 客户端主动连服务器（出站，无需开放入站端口）。服务器按"房间码"配对两个玩家并
 * 双向转发消息。
 *
 * 协议 (文本行, \n 结尾):
 *   客户端→服务器:
 *     CREATE                开房（服务器返回 ROOM <code>）
 *     JOIN <code>           加入房间（配对上后开始转发）
 *     BYE                   退出
 *     <任意游戏消息>         配对后由服务器转发给对方（加 "PEER " 前缀）
 *   服务器→客户端:
 *     ROOM <code>           开房成功，房间码
 *     PEER <msg>            对方转发的消息
 *     PEER BYE              对方已退出
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

static std::atomic<bool> g_running{true};
static std::mutex g_mtx;
static std::atomic<int> g_roomSeq{1000};
static std::map<int, std::pair<SOCKET,SOCKET>> g_rooms;   // code -> {A(房主), B(加入)}

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

// 双向转发两个已配对的 socket，直到一方断开
static void relayPair(SOCKET a, SOCKET b){
    std::string bufA, bufB;
    std::atomic<bool> aAlive{true}, bAlive{true};
    std::thread t1([&](){
        std::string line;
        while(g_running && bAlive.load()){
            if(recvLine(a, bufA, line)){
                if(line=="BYE"){ sendLine(b,"PEER BYE"); break; }
                if(line=="CREATE" || line.rfind("JOIN ",0)==0) continue;   // 控制消息不转发
                sendLine(b, "PEER "+line);
            } else break;
        }
        aAlive.store(false);
    });
    std::thread t2([&](){
        std::string line;
        while(g_running && aAlive.load()){
            if(recvLine(b, bufB, line)){
                if(line=="BYE"){ sendLine(a,"PEER BYE"); break; }
                if(line=="CREATE" || line.rfind("JOIN ",0)==0) continue;
                sendLine(a, "PEER "+line);
            } else break;
        }
        bAlive.store(false);
    });
    t1.join(); t2.join();
    closeSock(a); closeSock(b);
}

// 房主等待加入方，配对上后开始转发
static void waitForJoin(SOCKET a, int code){
    while(g_running){
        SOCKET b = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            auto it = g_rooms.find(code);
            if(it != g_rooms.end() && it->second.second != INVALID_SOCKET){
                b = it->second.second;
                g_rooms.erase(it);
            } else if(it == g_rooms.end()){
                closeSock(a);
                return;
            }
        }
        if(b != INVALID_SOCKET){ relayPair(a, b); return; }
        Sleep(100);
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
    SOCKET ls = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if(ls==INVALID_SOCKET) ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(ls==INVALID_SOCKET){ printf("socket failed\n"); return 1; }
    int reuse=1; setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof reuse);
    int v6only=0; setsockopt(ls,IPPROTO_IPV6,IPV6_V6ONLY,(const char*)&v6only,sizeof v6only);
    sockaddr_in6 a6{}; a6.sin6_family=AF_INET6; a6.sin6_addr=in6addr_any; a6.sin6_port=htons((u_short)port);
    if(bind(ls,(sockaddr*)&a6,sizeof a6)!=0){
        printf("bind [::]:%d failed (port in use?)\n", port); return 1;
    }
    listen(ls,SOMAXCONN);
    printf("=== Binary Tree Battle Room Server ===\n");
    printf("Listening: [::]:%d  (IPv6 + IPv4 dual-stack)\n", port);
    openFirewallPort(port);
    printf("Waiting for players...\n");

    while(g_running){
        sockaddr_in6 ca{}; int al=sizeof ca;
        SOCKET c = accept(ls,(sockaddr*)&ca,&al);
        if(c==INVALID_SOCKET) continue;
        std::string pending, line;
        if(!recvLine(c, pending, line)){ closeSock(c); continue; }
        if(line=="CREATE"){
            int code = ++g_roomSeq;
            { std::lock_guard<std::mutex> lk(g_mtx); g_rooms[code] = {c, INVALID_SOCKET}; }
            char r[32]; snprintf(r,sizeof r,"ROOM %d\n",code);
            sendLine(c, std::string(r));
            printf("[room %d] created\n", code);
            std::thread(waitForJoin, c, code).detach();
        } else if(line.rfind("JOIN ",0)==0){
            int code = atoi(line.c_str()+5);
            bool ok=false;
            { std::lock_guard<std::mutex> lk(g_mtx);
              auto it=g_rooms.find(code);
              if(it!=g_rooms.end() && it->second.second==INVALID_SOCKET){
                  it->second.second = c;   // 加入方接管转发线程的 b
                  ok = true;
              }
            }
            if(!ok){ sendLine(c,"PEER BYE"); closeSock(c); printf("[join %d] room not found/full\n",code); }
            else printf("[room %d] joined -> relaying\n", code);
        } else {
            closeSock(c);
        }
    }
    closesocket(ls);
    WSACleanup();
    return 0;
}
