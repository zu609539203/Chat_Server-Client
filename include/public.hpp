#ifndef _PUBLIC_H_
#define _PUBLIC_H_

enum EnMsgType
{
    LOGIN_MSG = 1,  // 登录消息 msgid = 1
    LOGIN_MSG_ACK,  // 登录返回值
    REG_MSG,        // 注册消息 msgid = 3
    REG_MSG_ACK,    // 注册返回值
    ONE_CHAT_MSG,   // 聊天消息 msgid = 5
    ADD_FRIEND_MSG, // 添加好友 msgid = 6

    CREATE_GROUP_MSG,   // 创建群组   msgid = 7
    ADD_GROUP_MSG,      // 加入群组   msgid = 8
    GROUP_CHAT_MSG,     // 群聊天     msgid = 9

    LOGOUT_MSG,         // 注销消息   msgid = 10
};


#endif