#include "chatserver.hpp"
#include "chatservice.hpp"
#include <iostream>
#include <signal.h>
using namespace std;

// 处理服务器接收 ctrl + c 信号退出程序时，重置user的状态信息
void resetHanlder(int)
{
    ChatService::instance()->reset();
    exit(0) ;
}

int main(int argc, char** argv)
{
    // 命令行解析传入的参数
    char* ip = argv[1];
    uint16_t port = atoi(argv[2]);

    signal(SIGINT, resetHanlder);

    EventLoop loop;
    InetAddress addr(ip, port);
    ChatServer server(&loop, addr, "ChatServer");

    server.start();
    loop.loop();

    return 0;
}