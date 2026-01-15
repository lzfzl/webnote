#include <time.h>
#include <ctime>
#include "httphandler.h"
const int LATENT = 4;
class timenode{
public:
    timenode(http_handler *conn);
    time_t ddl;
    timenode *pre;
    timenode *next;
    int getFd();
    void remove();
private:
    http_handler *data;
};

class timer{
public:
    timer();
    bool addConn(http_handler *conn);
    bool adjustConn(http_handler *conn);
    bool removeConn(http_handler *conn);
    void clear();
    timenode* findConn(http_handler *conn);
    bool insertConn(timenode* i);
private:
    timenode* dummy;
};
