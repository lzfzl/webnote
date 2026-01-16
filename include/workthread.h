#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include "httphandler.h"
const int MAX_WORKER_NUMBER = 4;
class threadpoll{
public:
    threadpoll(int thread_num = MAX_WORKER_NUMBER);
    void addTask(http_handler* hh,int RorW);
private:
    std::queue<http_handler*> task;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<std::thread> workers;
    bool m_stop;
};