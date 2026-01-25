#include "workthread.h"


threadpoll::threadpoll(sqlPools *sqlpool,int thread_num):m_sqlpool(sqlpool){
    if(thread_num<=0)thread_num=1;
    workers.reserve(thread_num);
    for(int i=0;i<thread_num;i++){
        workers.emplace_back([this]{
            while (true)
            {
                http_handler* cur_hh;
                {
                    std::unique_lock<std::mutex> lock_(m_mutex);
                    m_cv.wait(lock_,[this]{return m_stop||!task.empty();});
                    cur_hh = task.front();
                    task.pop();
                }
                if(cur_hh->RorW==0){
                    if(!cur_hh->process()){
                        cur_hh->close_conn();
                        continue;
                    }
                    if(cur_hh->m_method&&strcasecmp(cur_hh->m_method,"OPTIONS")&&cur_hh->m_url=="/api/login"){
                        if(!cur_hh->sqlconn)cur_hh->sqlconn = m_sqlpool->getaConnect();
                        cur_hh->checkLogin(); 
                    }
                    else if(cur_hh->m_url=="/api/data"){
                        if(!cur_hh->sqlconn)cur_hh->sqlconn = m_sqlpool->getaConnect();
                        // cur_hh->lodaData(); 
                    }
                    if(!cur_hh->process_write()){
                        cur_hh->close_conn();
                        continue;
                    }
                    if(!cur_hh->write()){
                        cur_hh->close_conn();
                        continue;
                    }
                }
                else{
                    if(!cur_hh->write()){
                        cur_hh->close_conn();
                        continue;
                    }
                }
            }
        });
    }
}
void threadpoll::addTask(http_handler* hh,int RorW){
    std::unique_lock<std::mutex> lock_(m_mutex);
    hh->RorW = RorW;
    task.push(hh);
    m_cv.notify_one();
}