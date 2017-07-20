// http_client.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "event_thread.h"
#include "conn_pool.h"
#include "http_client.h"

#include <iostream>
#include <string>

class DummyHandler : public HttpResponseHandler
{
    HANDLE m_event;
public:
    DummyHandler()
    {
        m_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    }
    ~DummyHandler()
    {
        CloseHandle(m_event);
    }

    void Wait()
    {
        WaitForSingleObject(m_event, INFINITE);
    }

    virtual void onError()
    {
        std::cout << "onError" << std::endl;
        SetEvent(m_event);
    }
    virtual void onStatus(
        int code)
    {
        std::cout << "onStatus code=" << code << std::endl;
    }
    virtual void onHeader(
        const char *name, 
        unsigned int nameLen,
        const char *value,
        unsigned int valueLen)
    {
        std::cout << "onHeader " << name << ": " << value << std::endl;
    }
    virtual void onHeaderEnd()
    {
        std::cout << "onHeaderEnd" << std::endl;
    }
    virtual void onBody(
        const void *data,
        unsigned int len)
    {
        std::cout << "onBody len=" << len << std::endl;
    }
    virtual void onEnd()
    {
        std::cout << "onEnd" << std::endl;
        SetEvent(m_event);
    }
};

int main(int argc, char* argv[])
{
    nanoev_init();

    EventThread eventThread;
    eventThread.start();

    ConnectionPool connPool;
    connPool.Init(100, 120, &eventThread);

    DummyHandler handler;
    HttpClient client;
    client.Init(16384, 16384, &eventThread, &connPool, &handler);

    std::string URL;
    while (true)
    {
        std::cout << ">>> URL(q to quit): ";
        std::cin >> URL;
        std::cout << std::endl;
        if (URL == "q" || URL == "Q")
            break;

        if (client.Open(HTTP_METHOD_GET, URL.c_str()))
        {
            if (client.Execute())
                handler.Wait();
            else
                std::cout << "ERR: HTTClient::Execute failed" << std::endl;
        }
        else
        {
            std::cout << "ERR: HTTPClient::Open failed" << std::endl;
        }
        std::cout << std::endl;
    }

    connPool.Term();
    eventThread.stop();
    
    nanoev_term();

    return 0;
}
