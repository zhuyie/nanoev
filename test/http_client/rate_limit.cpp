#include "stdafx.h"
#include "rate_limit.h"

//------------------------------------------------------------------------------

#define tickInterval 20 // 毫秒

RateLimiters::RateLimiters()
{
    InitializeCriticalSection(&m_cs);
    m_thread = NULL;
    m_timer = NULL;
}

RateLimiters::~RateLimiters()
{
    assert(m_cont.empty());
    assert(m_timer == NULL);
    DeleteCriticalSection(&m_cs);
}

void RateLimiters::Init(EventThread *thread)
{
    assert(thread);
    m_thread = thread;
    m_thread->queueTask(new SimpleEventTask(__startup, this));
}

void RateLimiters::Term()
{
    assert(m_thread);
    m_thread->queueTask(new SimpleEventTask(__shutdown, this));
}

RateLimiter* RateLimiters::Create(double rate, double burst)
{
    RateLimiter *r = new RateLimiter;
    if (!r->Init(rate, burst))
    {
        delete r;
        return NULL;
    }
    EnterCriticalSection(&m_cs);
    m_cont.insert(r);
    LeaveCriticalSection(&m_cs);
    return r;
}

bool RateLimiters::Destory(RateLimiter* r)
{
    bool res = false;
    EnterCriticalSection(&m_cs);
    ContT::iterator iter = m_cont.find(r);
    if (iter != m_cont.end())
    {
        m_cont.erase(iter);
        delete r;
        res = true;
    }
    LeaveCriticalSection(&m_cs);
    return res;
}

void RateLimiters::__startup(nanoev_loop *loop, void *ctx)
{
    RateLimiters *self = (RateLimiters*)ctx;

    self->m_timer = nanoev_event_new(nanoev_event_timer, loop, self);
    
    struct nanoev_timeval after = { 0, tickInterval*1000 };
    nanoev_timer_add(self->m_timer, after, 1, __onTimer);
}

void RateLimiters::__shutdown(nanoev_loop *loop, void *ctx)
{
    RateLimiters *self = (RateLimiters*)ctx;

    if (self->m_timer)
    {
        nanoev_event_free(self->m_timer);
        self->m_timer = NULL;
    }
}

void RateLimiters::__onTimer(nanoev_event *timer)
{
    RateLimiters *self = (RateLimiters*)nanoev_event_userdata(timer);
    EnterCriticalSection(&self->m_cs);
    ContT::iterator iter = self->m_cont.begin();
    for (; iter != self->m_cont.end(); ++iter)
    {
        (*iter)->Tick();
    }
    LeaveCriticalSection(&self->m_cs);
}

//------------------------------------------------------------------------------

RateLimiter::RateLimiter()
{
    m_rate = 0;
    m_value = 0;
}

RateLimiter::~RateLimiter()
{
}

bool RateLimiter::Init(double rate, double burst)
{
    if (rate <= 0 || burst <= 0)
        return false;
    m_rate = rate / 1000 * tickInterval; // 换算为每个tick的新增量
    m_burst = burst;
    return true;
}

void RateLimiter::Tick()
{
    m_value = m_value + m_rate;
    if (m_value > m_burst)
        m_value = m_burst;
}

unsigned int RateLimiter::Take(unsigned int want)
{
    unsigned int n = (unsigned int)m_value;
    if (n > want)
        n = want;
    m_value = m_value - n;
    return n;
}
