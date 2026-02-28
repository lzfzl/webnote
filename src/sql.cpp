#include "sql_conn_pool.h"

sqlPools::sqlPools(const std::string& host,const std::string& user,const std::string& passwd,const std::string& db,int poolsize):
    host_(host),
    user_(user),
    password_(passwd),
    db_(db),
    poolSize_(poolsize)
{
    for(int i=0;i<poolSize_;i++){
        sql::mysql::MySQL_Driver *driver = sql::mysql::get_driver_instance();
        sql::Connection * con = driver->connect(host_,user_,password_);
        con->setSchema(db_);
        connectionPool_.push(con);
    }
}
    

sql::Connection* sqlPools::getaConnect(){
    std::unique_lock<std::mutex> lock(poolMutex_);
    while(connectionPool_.empty()){
        poolCondition_.wait(lock);
    }
    sql::Connection *conn = connectionPool_.front();
    connectionPool_.pop();
    return conn;
}
    void sqlPools::releaseConnect(sql::Connection* conn){
        if(!conn) return;
        std::unique_lock<std::mutex> lock(poolMutex_);
        connectionPool_.push(conn);
        poolCondition_.notify_one();
    }
    sqlPools::~sqlPools(){
        while (!connectionPool_.empty())
        {
            sql::Connection* c = connectionPool_.front();
            connectionPool_.pop();
            delete c;
        }    
    }
