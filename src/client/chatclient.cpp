#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <functional>

using namespace std;
using json = nlohmann::json;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <atomic>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

// 记录当前系统登录的用户信息
User g_currentUser;
// 记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
// 记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;
// 显示当前登录成功用户的基本信息
void showCurrentUserData();
// 控制主菜单程序是否运行
bool isMainMenuRunning = false;

// 接收线程
void readTaskHandler(int clientfd);
// 获取系统时间
string getCurrentTime();
// 主聊天页面
void mainMenu(int);

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example : ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析传入的命令行参数： ip  port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建client端的socket
    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd == -1)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    // 填写client需要连接的server信息 ip + port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server.sin_addr.s_addr);

    // client 和 server 进行连接
    if (-1 == connect(cfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(cfd);
        exit(-1);
    }

    for (;;)
    {
        // 显示首页面菜单 登录、注册、退出
        cout << "========================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "========================" << endl;
        cout << "choice:";
        int choice = 0;
        cin >> choice;
        cin.get(); // 读掉缓冲区残留的回车

        switch (choice)
        {
        case 1:
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "userid: ";
            cin >> id;
            cin.get();
            cout << "user_password: ";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();

            int len = send(cfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send login msg error:" << request << endl;
            }
            else
            {
                char buffer[1024] = {0};
                len = recv(cfd, buffer, 1024, 0);
                if (-1 == len) // 未能正确接收到消息
                {
                    cerr << "recv login response error" << endl;
                }
                else // 成功接收到消息
                {
                    json responsejs = json::parse(buffer);
                    if (0 != responsejs["errno"].get<int>()) // 登录失败
                    {
                        cerr << responsejs["errmsg"] << endl;
                    }
                    else //* 登录成功
                    {
                        // 记录当前用户的 id 和 name
                        g_currentUser.setId(responsejs["id"].get<int>());
                        g_currentUser.setName(responsejs["name"]);

                        // 记录当前用户的好友列表信息
                        if (responsejs.contains("friends"))
                        {
                            // 初始化
                            g_currentUserFriendList.clear();

                            // 是否包含 "friends" 这个键
                            vector<string> vec = responsejs["friends"];
                            for (string &str : vec)
                            {
                                json js = json::parse(str);
                                User user;
                                user.setId(js["id"].get<int>());
                                user.setName(js["name"]);
                                user.setState(js["state"]);
                                g_currentUserFriendList.push_back(user);
                            }
                        }

                        // 记录当前用户的群组列表信息
                        if (responsejs.contains("groups"))
                        {
                            // 初始化
                            g_currentUserGroupList.clear();

                            // 是否包含 "groups" 这个键
                            vector<string> vec = responsejs["groups"];
                            for (string &groupstr : vec)
                            {
                                json js = json::parse(groupstr);
                                Group group;
                                group.setId(js["id"].get<int>());
                                group.setName(js["groupname"]);
                                group.setDesc(js["groupdesc"]);

                                vector<string> vec2 = js["users"];
                                for (string &userstr : vec2)
                                {
                                    GroupUser groupuser;
                                    json js = json::parse(userstr);
                                    groupuser.setId(js["id"].get<int>());
                                    groupuser.setName(js["name"]);
                                    groupuser.setState(js["state"]);
                                    groupuser.setRole(js["role"]);
                                    group.getUser().push_back(groupuser);
                                }

                                g_currentUserGroupList.push_back(group);
                            }
                        }

                        // 打印登录用户的基本信息
                        showCurrentUserData();

                        // 显示当前用户的离线消息 个人聊天信息或者群组消息
                        if (responsejs.contains("offlinemsg"))
                        {
                            vector<string> vec = responsejs["offlinemsg"];
                            for (string &str : vec)
                            {
                                json js = json::parse(str);

                                int msgtype = js["msgid"].get<int>();
                                if (ONE_CHAT_MSG == msgtype)
                                {
                                    // <time> [id] name said: xxx
                                    cout << "<" << js["time"].get<string>() << ">"
                                         << " [" << js["id"] << "]"
                                         << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
                                }

                                if (GROUP_CHAT_MSG == msgtype)
                                {
                                    // {群消息 : id} <time> [id] name said: xxx
                                    cout << "{群消息:" << js["groupid"] << "} : "
                                         << "<" << js["time"].get<string>() << ">"
                                         << " [" << js["id"] << "]"
                                         << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
                                }
                            }
                        }

                        // 登录成功，启动接收线程负责接收数据
                        // TODO : 如何控制 当前用户注销后，第二次进入该循环中时，重启一个新的线程，而上一个线程陷入阻塞，浪费资源
                        static int readthreadnumber = 0;
                        if (readthreadnumber == 0)
                        {
                            std::thread readTask(readTaskHandler, cfd);
                            readTask.detach();
                            readthreadnumber++;     // 保证只有一个线程开启
                        }

                        // 进入聊天主菜单界面
                        isMainMenuRunning = true;
                        mainMenu(cfd);
                    }
                }
            }
        }
        break;
        case 2: // register 注册业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username: ";
            cin.getline(name, 50);
            cout << "userpassword: ";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();

            int len = send(cfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send reg msg error:" << request << endl;
            }
            else
            {
                char buffer[1024] = {0};
                len = recv(cfd, buffer, 1024, 0);
                if (-1 == len) // 未能正确接收到消息
                {
                    cerr << "recv register response error" << endl;
                }
                else
                {
                    json responsejs = json::parse(buffer);
                    if (0 != responsejs["errno"].get<int>()) // 注册失败
                    {
                        cerr << name << " is already exit, register fail! " << endl;
                    }
                    else // 注册成功
                    {
                        cout << name << " register success, userid is " << responsejs["id"]
                             << ", must remember it! " << endl;
                    }
                }
            }
        }
        break;
        case 3: // quit 退出业务
        {
            close(cfd);
            exit(0);
        }
        break;
        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }

    return 0;
}

// 显示当前登录成功用户的基本信息
void showCurrentUserData()
{
    cout << "===============login user================" << endl;
    cout << "current login user => id : " << g_currentUser.getId() << " name : " << g_currentUser.getName() << endl;
    cout << "==============friend list===============" << endl;
    if (!g_currentUserFriendList.empty())
    {
        // 显示好友信息
        for (User &user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "==============group list===============" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group &group : g_currentUserGroupList)
        {
            // 显示组的信息
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            // 显示组内成员信息
            for (GroupUser &user : group.getUser())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState()
                     << " " << user.getRole() << endl;
            }
        }
    }
    cout << "=======================================" << endl;
}

// 接收线程
void readTaskHandler(int cfd)
{
    while (1)
    {
        char buffer[1024] = {0};
        int len = recv(cfd, buffer, 1024, 0);
        if (-1 == len || 0 == len)
        {
            close(cfd);
            exit(-1);
        }

        // 接收ChatServer转发的数据，反序列化生成json对象
        json js = json::parse(buffer);
        int msgtype = js["msgid"].get<int>();
        if (ONE_CHAT_MSG == msgtype)
        {
            // <time> [id] name said: xxx
            cout << "<" << js["time"].get<string>() << ">"
                 << " [" << js["id"] << "]"
                 << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
        }

        if (GROUP_CHAT_MSG == msgtype)
        {
            // {群消息 : id} <time> [id] name said: xxx
            cout << "{群消息:" << js["groupid"] << "} : "
                 << "<" << js["time"].get<string>() << ">"
                 << " [" << js["id"] << "]"
                 << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
        }
    }
}

// "help" command handler
void help(int fd = 0, string str = "");
// "chat" command handler
void chat(int, string);
// "addfriend" command handler
void addfriend(int, string);
// "creategroup" command handler
void creategroup(int, string);
// "addgroup" command handler
void addgroup(int, string);
// "groupchat" command handler
void groupchat(int, string);
// "logout" command handler
void logout(int, string);

// 系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令, 格式help"},
    {"chat", "一对一聊天, 格式chat:friendid:message"},
    {"addfriend", "添加好友, 格式addfriend:friendid"},
    {"creategroup", "创建群组, 格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组, 格式addgroup:groupid"},
    {"groupchat", "群聊, 格式groupchat:groupid:message"},
    {"logout", "注销, 格式logout"}};

// 注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"logout", logout}};

// 主聊天页面程序
void mainMenu(int cfd)
{
    help();

    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command; // 存储命令
        int idx = commandbuf.find(":");
        if (-1 == idx)
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0, idx);
        }

        auto iter = commandHandlerMap.find(command);
        if (iter == commandHandlerMap.end())
        {
            cerr << "invalid input command! " << endl;
            continue;
        }

        // 调用相应命令的事件处理回调，mainMenu对修改封闭，添加新功能不需要修改该函数
        iter->second(cfd, commandbuf.substr(idx + 1, commandbuf.size() - idx));
    }
}

// "help" command handler
void help(int, string)
{
    cout << "show command list: " << endl;
    for (auto &p : commandMap)
    {
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}

// "chat" command handler : 格式chat:friendid:message
void chat(int cfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }
    int friendid = stoi(str.substr(0, idx));
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(cfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send chat msg error : " << buffer << endl;
    }
}

// "addfriend" command handler : 格式addfriend:friendid
void addfriend(int cfd, string str)
{
    int friendid = stoi(str);
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer = js.dump();

    int len = send(cfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addfriend msg error : " << buffer << endl;
    }
}

// "creategroup" command handler  :  格式creategroup:groupname:groupdesc
void creategroup(int cfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "creategroup command is valid!" << endl;
        return;
    }

    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - 1 - idx);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    int len = send(cfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send creategroup msg error : " << buffer << endl;
    }
}

// "addgroup" command handler   :  格式addgroup:groupid
void addgroup(int cfd, string str)
{
    int groupid = stoi(str);
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;
    string buffer = js.dump();

    int len = send(cfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addgroup msg error : " << buffer << endl;
    }
}

// "groupchat" command handler  :   格式groupchat:groupid:message
void groupchat(int cfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "groupchat command is valid!" << endl;
        return;
    }

    int groupid = stoi(str.substr(0, idx));
    string message = str.substr(idx + 1, str.size() - 1 - idx);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(cfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send groupchat msg error : " << buffer << endl;
    }
}

// "logout" command handler     :   格式logout
void logout(int cfd, string str)
{
    json js;
    js["msgid"] = LOGOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(cfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send logout msg error : " << buffer << endl;
    }
    else
    {
        isMainMenuRunning = false;
    }
}

// 获取系统时间
string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}