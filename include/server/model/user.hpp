#ifndef _USER_H_
#define _USER_H_

#include <string>
using namespace std;

// 匹配 User 表的 ORM 类 ，ORM 全称 Object Relational Mapping ，对象关系映射
// 通过 ORM 我们可以通过类的方式去操作数据库，而不用再写原生的SQL语句
class User
{
public:
    User(int id = -1, string name = "", string pwd = "", string state = "offline")
    {
        this->id = id;
        this->name = name;
        this->password = pwd;
        this->state = state;
    }

    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setPwd(string pwd) { this->password = pwd; }
    void setState(string state) { this->state = state; }

    int getId() { return this->id; }
    string getName() { return this->name; }
    string getPwd() { return this->password; }
    string getState() { return this->state; }

protected:
    int id;
    std::string name;
    std::string password;
    std::string state;
};

#endif
