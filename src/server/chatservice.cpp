#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>

#include <iostream>
using namespace std;

ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把 online 状态的用户设置成 offline
    _usermodel.resetState();
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});              // 1
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});                  // 3
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});         // 5
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});     // 6
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)}); // 7
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});       // 8
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});     // 9
    _msgHandlerMap.insert({LOGOUT_MSG, std::bind(&ChatService::logout, this, _1, _2, _3)});            // 10

    // 连接 redis 服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    // 记录错误日志，msgid没有对应的事件回调处理
    auto iter = _msgHandlerMap.find(msgid);
    if (iter == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器 空操作
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp time)
        {
            LOG_ERROR << "msgid: " << msgid << " can't find handler!";
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}

// 处理登录业务
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = _usermodel.query(id);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 登录失败：用户已登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "该账号已经登录，请勿重复登录！";
            conn->send(response.dump());
        }
        else
        {
            // 登录成功，记录用户连接信息
            {
                // 上锁，保证多线程安全
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // id用户订阅成功后，向redis订阅channel(id)
            _redis.subscribe(id);

            // 登录成功，更新用户状态信息：offline -> online
            user.setState("online");
            _usermodel.updateState(user);

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();

            // 查询该用户是否有离线消息
            vector<string> vec = _offlinemsgmodel.query(user.getId());
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;
                // 读取该用户的离线消息后，及时删除数据库中存储的离线消息
                _offlinemsgmodel.remove(id);
            }

            // 查询该用户的好友信息(id, name, state)并返回
            vector<User> userVec = _friendmodel.query(id);
            if (!userVec.empty())
            {
                vector<string> vec2;
                for (User &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            // 查询用户的群组信息
            vector<Group> groupVec = _groupmodel.queryGroups(id);
            if (!groupVec.empty())
            {
                vector<string> groupV;
                for (Group &group : groupVec)
                {
                    json grpjson;
                    grpjson["id"] = group.getId();
                    grpjson["groupname"] = group.getName();
                    grpjson["groupdesc"] = group.getDesc();

                    vector<string> userV;
                    for (GroupUser &user : group.getUser())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    grpjson["users"] = userV;
                    groupV.push_back(grpjson.dump());
                }
                response["groups"] = groupV;
            }

            conn->send(response.dump());
        }
    }
    else
    {
        // 登录失败：用户名或密码错误 / 不存在该用户
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "用户名或密码错误！";
        conn->send(response.dump());
    }
}

// 处理注册业务
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _usermodel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        // 上锁，保证多线程安全
        lock_guard<mutex> lock(_connMutex);
        for (auto iter = _userConnMap.begin(); iter != _userConnMap.end(); ++iter)
        {
            if (iter->second == conn)
            {
                // 从map表中删除用户的链接信息
                user.setId(iter->first);
                _userConnMap.erase(iter);
                break;
            }
        }
    }

    // 用户注销，在redis服务器中下线，在redis中取消订阅
    _redis.unsubscribe(user.getId());

    // 更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");
        _usermodel.updateState(user);
    }
}

// 用户注销
void ChatService::logout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    auto iter = _userConnMap.find(id);
    if (iter != _userConnMap.end())
    {
        _userConnMap.erase(iter);
    }

    // 用户注销，在redis服务器中下线，在redis中取消订阅
    _redis.unsubscribe(id);

    // 更新用户的状态信息
    User user(id, " ", " ", "offline");
    _usermodel.updateState(user);
}

// 一对一聊天业务       msgid   id  name  toid  msg  time
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // 得到toid用户的id
    int toid = js["toid"].get<int>();

    // 上锁：为了防止多线程同时对 在线客户表 做修改
    {
        lock_guard<mutex> lock(_connMutex);
        auto iter = _userConnMap.find(toid);
        // toid用户在线
        if (iter != _userConnMap.end())
        {
            // toid在线，转发消息   服务器主动推送消息给toid用户
            iter->second->send(js.dump());
            return;
        }
    }

    // 当在userConnMap中未找到通信用户的信息，有两种情况：1.该用户登录在其他服务器上;  2.该用户不在线
    // toid在线，中间件转发
    User user = _usermodel.query(toid);
    if (user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    // toid不在线，存储离线消息
    _offlinemsgmodel.insert(toid, js.dump());
}

// 添加好友业务     msgid   id  friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    // 存储好友信息
    _friendmodel.insert(userid, friendid);
}

// 创建群组业务     id  groupname   groupdesc   userid, group.getId(), "creator"
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupmodel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupmodel.addGroup(userid, group.getId(), "creator");
    }
}

// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupmodel.addGroup(groupid, userid, "normal");
}

// 群组聊天
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupmodel.queryGroupUsers(userid, groupid);

    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec)
    {
        auto iter = _userConnMap.find(id);
        if (iter != _userConnMap.end())
        {
            // 转发群消息
            iter->second->send(js.dump());
        }
        else
        {
            // toid在线，中间件转发
            User user = _usermodel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            else
            {
                // 存储离线消息
                _offlinemsgmodel.insert(id, js.dump());
            }
        }
    }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto iter = _userConnMap.find(userid);
    if (iter != _userConnMap.end())
    {
        iter->second->send(msg);
        return;
    }

    // 存储该用户的离线消息
    _offlinemsgmodel.insert(userid, msg);
}
