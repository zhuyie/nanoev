#include "stdafx.h"
#include "conn_pool.h"

//------------------------------------------------------------------------------

ConnectionPool::ConnectionPool()
{
    m_capacity = 0;
    m_maxSeconds = 0;
    m_thread = NULL;
    m_timer = NULL;
}

ConnectionPool::~ConnectionPool()
{
    assert(m_cont.empty());
    assert(m_timer == NULL);
}

void ConnectionPool::Init(unsigned int capacity, unsigned int maxSeconds, EventThread *thread)
{
    assert(capacity && maxSeconds && thread);
    m_capacity = capacity;
    m_maxSeconds = maxSeconds;
    m_thread = thread;
    m_thread->queueTask(new SimpleEventTask(__startup, this));
}

void ConnectionPool::Term()
{
    assert(m_thread);
    m_thread->queueTask(new SimpleEventTask(__shutdown, this));
}

nanoev_event* ConnectionPool::Get(const std::string &host, unsigned short port)
{
    nanoev_event *conn = NULL;
    ContT::const_iterator iter = m_cont.begin();
    for (; iter != m_cont.end(); ++iter)
    {
        const entry &e = *iter;
        if (e.host == host && e.port == port)
        {
            conn = e.conn;
            m_cont.erase(iter);
            break;
        }
    }
    return conn;
}

void ConnectionPool::Put(const std::string &host, unsigned short port, nanoev_event *conn)
{
    assert(conn);

    nanoev_event_set_userdata(conn, NULL);

    if (m_cont.size() >= m_capacity)
    {
        ContT::const_iterator iter = m_cont.begin();
        nanoev_event_free((*iter).conn);
        m_cont.erase(iter);
    }

    entry e;
    e.host = host;
    e.port = port;
    e.conn = conn;
    e.seconds = 0;
    m_cont.push_back(e);
}

void ConnectionPool::Clear()
{
    ContT::const_iterator iter = m_cont.begin();
    for (; iter != m_cont.end(); ++iter)
    {
        const entry &e = *iter;
        nanoev_event_free((*iter).conn);
    }
    m_cont.clear();
}

//------------------------------------------------------------------------------

void ConnectionPool::__startup(nanoev_loop *loop, void *ctx)
{
    ConnectionPool *self = (ConnectionPool*)ctx;

    self->m_timer = nanoev_event_new(nanoev_event_timer, loop, self);
    
    struct nanoev_timeval after = { 1, 0 };
    nanoev_timer_add(self->m_timer, after, 1, __onTimer);
}

void ConnectionPool::__shutdown(nanoev_loop *loop, void *ctx)
{
    ConnectionPool *self = (ConnectionPool*)ctx;

    if (self->m_timer)
    {
        nanoev_event_free(self->m_timer);
        self->m_timer = NULL;
    }

    self->Clear();
}

void ConnectionPool::__onTimer(nanoev_event *timer)
{
    ConnectionPool *self = (ConnectionPool*)nanoev_event_userdata(timer);

    for (ContT::iterator iter = self->m_cont.begin(); iter != self->m_cont.end();)
    {
        entry &e = *iter;
        if (e.seconds >= self->m_maxSeconds)
        {
            nanoev_event_free(e.conn);
            iter = self->m_cont.erase(iter);
        }
        else
        {
            e.seconds++;
            ++iter;
        }
    }
}
