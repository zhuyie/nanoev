#ifndef __HTTP_CLIENT_H__
#define __HTTP_CLIENT_H__

#include <string>
#include "nanoev.hpp"
#include "http_parser/http_parser.h"
#include "handler.h"
#include "event_thread.h"
#include "conn_pool.h"
#include "rate_limit.h"

//------------------------------------------------------------------------------

typedef enum {
    HTTP_METHOD_GET = 1,
    HTTP_METHOD_HEAD = 2,
    HTTP_METHOD_POST = 3,

    _HTTP_METHOD_MIN = HTTP_METHOD_GET,
    _HTTP_METHOD_MAX = HTTP_METHOD_POST,
} HTTP_METHOD;

class HttpClient
{
    typedef enum {
        invalid = 0,
        init,         // Init完毕
        open,         // 新开始了一个Request
        requestReady, // Request已经准备完毕
        execute,      // 开始执行
        done,         // 执行完毕
    } stage;
    
    typedef enum {
        none = 0,
        field,
        value,
    } header_cb_type;

    unsigned int m_requestMaxSize;
    char *m_request;
    unsigned int m_requestSize;
    unsigned int m_requestSent;

    unsigned int m_recvBufSize;
    char *m_recvBuf;

    EventThread *m_thread;
    ConnectionPool *m_connPool;
    RateLimiter *m_rateLimiter;
    HttpResponseHandler *m_handler;

    stage m_stage;

    HTTP_METHOD m_method;
    std::string m_URL;
    struct http_parser_url m_parsedURL;
    std::string m_host;
    unsigned short m_port;
    std::string m_relativeURI;

    http_parser_settings m_parserSettings;
    http_parser m_parser;
    std::string m_headerField;
    std::string m_headerValue;
    header_cb_type m_lastHeaderCBType;
    bool m_isStatusNotified;
    bool m_isMessageComplete;

    nanoev_event *m_conn;
    bool m_isNewConn;
    
    nanoev_event *m_rateTimer;
    unsigned int m_rateTokens;

public:
    HttpClient();
    ~HttpClient();

    bool Init(
        unsigned int requestMaxSize, 
        unsigned int recvBufSize, 
        EventThread *thread, 
        ConnectionPool *connPool,
        RateLimiter *rateLimiter,
        HttpResponseHandler *handler
        );

    bool Open(HTTP_METHOD method, const char *URL);
    bool PutHeader(const char *field, const char *value);
    bool PutBody(const void *data, unsigned int len);
    bool Execute();

private:
    bool __parseURL(const char *URL);
    bool __writeRequestLine();
    bool __writeRequest(const void *data, unsigned int len);
    void __reset();

    void __onDone(bool isError);

    void __start(nanoev_loop *loop, bool useConnPool);
    void __onConnect(int status);
    void __onWrite(int status, void *buf, unsigned int bytes);
    void __onRead(int status, void *buf, unsigned int bytes);
    unsigned int __prepareReadTokens();
    void __consumeReadTokens(unsigned int n);
    void __onRateTimer();
    static void __start(nanoev_loop *loop, void *ctx);
    static void __onConnect(nanoev_event *tcp, int status);
    static void __onWrite(nanoev_event *tcp, int status, void *buf, unsigned int bytes);
    static void __onRead(nanoev_event *tcp, int status, void *buf, unsigned int bytes);
    static void __onTimer(nanoev_event *timer);

    static int __onHeaderField(http_parser *parser, const char *at, size_t length);
    static int __onHeaderValue(http_parser *parser, const char *at, size_t length);
    static int __onHeadersComplete(http_parser *parser);
    static int __onBody(http_parser *parser, const char *at, size_t length);
    static int __onMessageComplete(http_parser *parser);
};

//------------------------------------------------------------------------------

#endif // !__HTTP_CLIENT_H__
