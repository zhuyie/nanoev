#ifndef __CONN_POOL_H__
#define __CONN_POOL_H__

#include "nanoev.hpp"
#include "event_thread.h"
#include <string>
#include <vector>

//------------------------------------------------------------------------------

class ConnectionPool
{
    typedef struct {
        std::string host;
        unsigned short port;
        nanoev_event *conn;
        unsigned int seconds;
    } entry;
    typedef std::vector<entry> ContT;
    
    ContT m_cont;
    unsigned int m_capacity;
    unsigned int m_maxSeconds;
    EventThread *m_thread;
    nanoev_event *m_timer;

public:
    ConnectionPool();
    ~ConnectionPool();
    
    void Init(unsigned int capacity, unsigned int maxSeconds, EventThread *thread);
    void Term();

    nanoev_event* Get(const std::string &host, unsigned short port);
    void Put(const std::string &host, unsigned short port, nanoev_event *conn);
    void Clear();

private:
    static void __startup(nanoev_loop *loop, void *ctx);
    static void __shutdown(nanoev_loop *loop, void *ctx);
    static void __onTimer(nanoev_event *timer);
};

//------------------------------------------------------------------------------

#endif // !__CONN_POOL_H__
