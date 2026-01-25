#pragma once
#include <string>
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <queue>
#include <mutex>
#include <condition_variable>
class sqlPools
{
private:
    std::string host_, user_, password_, db_;
    int poolSize_;
    std::queue<sql::Connection*> connectionPool_;
    std::mutex poolMutex_;
    std::condition_variable poolCondition_;
public:
    sqlPools(const std::string& host,const std::string& user,const std::string& passwd,const std::string& db,int poolsize = 10);
    sql::Connection* getaConnect();
    void releaseConnect(sql::Connection* conn);
    ~sqlPools();
};