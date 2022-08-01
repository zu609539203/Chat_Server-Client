#ifndef _GROUP_H_
#define _GROUP_H_

#include "groupuser.hpp"
#include <string>
#include <vector>
using namespace std;

class Group
{
public:
    Group(int id = -1, string name = "", string desc = "")
    {
        this->id = id;
        this->name = name;
        this->desc = desc;
    }
 
    void setId(int id)  {this->id = id;}
    void setName(string name)   {this->name = name;}
    void setDesc(string desc)   {this->desc = desc;}

    int getId() {return this->id;}
    string getName() {return this->name;}
    string getDesc() {return this->desc;}
    // 将查出来的组员放入 vector , 便于后面的业务使用
    vector<GroupUser>   &getUser() {return this->user;}

private:
    int id;
    string name;
    string desc;
    vector<GroupUser>   user;
};

#endif