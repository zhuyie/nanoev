#ifndef __RATE_LIMIT_H__
#define __RATE_LIMIT_H__

#include "nanoev.hpp"
#include "event_thread.h"
#include <set>

//------------------------------------------------------------------------------

class RateLimiter;

class RateLimiters
{
    typedef std::set<RateLimiter*> ContT;

    CRITICAL_SECTION m_cs;
    ContT m_cont;
    EventThread *m_thread;
    nanoev_event *m_timer;

public:
    RateLimiters();
    ~RateLimiters();

    void Init(EventThread *thread);
    void Term();

    RateLimiter* Create(double rate, double burst);
    bool Destory(RateLimiter *r);

private:
    static void __startup(nanoev_loop *loop, void *ctx);
    static void __shutdown(nanoev_loop *loop, void *ctx);
    static void __onTimer(nanoev_event *timer);
};

//------------------------------------------------------------------------------

class RateLimiter
{
    friend class RateLimiters;

    double m_rate;
    double m_burst;
    double m_value;

public:
    RateLimiter();
    ~RateLimiter();

    unsigned int Take(unsigned int want);
    unsigned int WaitHint();

private:
    bool Init(double rate, double burst);
    void Tick();
};

//------------------------------------------------------------------------------

#endif // !__RATE_LIMIT_H__
