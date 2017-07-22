#include "stdafx.h"
#include "http_client.h"

//------------------------------------------------------------------------------

HttpClient::HttpClient()
{
    m_requestMaxSize = 0;
    m_request = NULL;

    m_recvBufSize = 0;
    m_recvBuf = 0;

    m_thread = NULL;
    m_connPool = NULL;
    m_rateLimiter = NULL;
    m_handler = NULL;

    m_stage = invalid;

    memset(&m_parserSettings, 0, sizeof(m_parserSettings));
    m_parserSettings.on_header_field = __onHeaderField;
    m_parserSettings.on_header_value = __onHeaderValue;
    m_parserSettings.on_headers_complete = __onHeadersComplete;
    m_parserSettings.on_body = __onBody;
    m_parserSettings.on_message_complete = __onMessageComplete;

    __reset();
}

HttpClient::~HttpClient()
{
    free(m_request);
    free(m_recvBuf);
}

bool HttpClient::Init(
    unsigned int requestMaxSize, 
    unsigned int recvBufSize, 
    EventThread *thread, 
    ConnectionPool *connPool,
    RateLimiter *rateLimiter,
    HttpResponseHandler *handler
    )
{
    assert(requestMaxSize && recvBufSize && thread && connPool && handler);
    assert(m_stage == invalid);

    m_requestMaxSize = requestMaxSize;
    m_request = (char*)calloc(m_requestMaxSize, 1);
    if (!m_request)
        return false;
    
    m_recvBufSize = recvBufSize;
    m_recvBuf = (char*)calloc(m_recvBufSize, 1);
    if (!m_recvBuf)
        return false;

    m_thread = thread;
    m_connPool = connPool;
    m_rateLimiter = rateLimiter;
    m_handler = handler;

    m_stage = init;

    return true;
}

bool HttpClient::Open(HTTP_METHOD method, const char *URL, bool useNewConn)
{
    assert(URL);

    if (m_stage < init || m_stage == execute)
        return false;

    __reset();

    if (method < _HTTP_METHOD_MIN || method > _HTTP_METHOD_MAX)
        return false;
    m_method = method;

    if (!__parseURL(URL))
        return false;
    if (!__writeRequestLine())
        return false;

    m_useNewConn = useNewConn;

    m_stage = open;

    return true;
}

bool HttpClient::PutHeader(const char *field, const char *value)
{
    assert(field && value);

    if (m_stage != open)
        return false;

    if (!__writeRequest(field, (unsigned int)strlen(field)))
        return false;
    if (!__writeRequest(": ", 2))
        return false;
    if (!__writeRequest(value, (unsigned int)strlen(value)))
        return false;
    if (!__writeRequest("\r\n", 2))
        return false;

    return true;
}

bool HttpClient::PutBody(const void *data, unsigned int len)
{
    assert(data && len);

    char str[50];
    _i64toa(len, str, 10);
    if (!PutHeader("Content-Length", str))
        return false;

    if (!__writeRequest("\r\n", 2))  // End of Headers
        return false;

    if (!__writeRequest(data, len))
        return false;

    m_stage = requestReady;

    return true;
}

bool HttpClient::Execute()
{
    if (m_stage == open)
    {
        // 没有body
        if (!__writeRequest("\r\n", 2))  // End of Headers
            return false;
        m_stage = requestReady;
    }
    if (m_stage != requestReady)
        return false;

    http_parser_init(&m_parser, HTTP_RESPONSE);
    m_parser.data = this;

    m_stage = execute;

    m_thread->queueTask(new SimpleEventTask(__start, this));

    return true;
}

//------------------------------------------------------------------------------

bool HttpClient::__parseURL(const char *URL)
{
    m_URL = URL;

    if (http_parser_parse_url(m_URL.c_str(), m_URL.size(), 0, &m_parsedURL) != 0)
        return false;

    if (m_parsedURL.field_set & (1 << UF_SCHEMA))
    {
        std::string schema(m_URL.c_str() + m_parsedURL.field_data[UF_SCHEMA].off, m_parsedURL.field_data[UF_SCHEMA].len);
        if (schema != "http")
            return false;
    }

    if ((m_parsedURL.field_set & (1 << UF_HOST)) == 0)
        return false;
    m_host.assign(m_URL.c_str() + m_parsedURL.field_data[UF_HOST].off, m_parsedURL.field_data[UF_HOST].len);

    if (m_parsedURL.field_set & (1 << UF_PORT))
        m_port = m_parsedURL.port;
    else
        m_port = 80;

    m_relativeURI.reserve(m_URL.length());
    m_relativeURI = "/";
    if (m_parsedURL.field_set & (1 << UF_PATH))
    {
        m_relativeURI.assign(m_URL.c_str() + m_parsedURL.field_data[UF_PATH].off, m_parsedURL.field_data[UF_PATH].len);
    }
    if (m_parsedURL.field_set & (1 << UF_QUERY))
    {
        m_relativeURI += "?";
        m_relativeURI.append(m_URL.c_str() + m_parsedURL.field_data[UF_QUERY].off, m_parsedURL.field_data[UF_QUERY].len);
    }
    if (m_parsedURL.field_set & (1 << UF_FRAGMENT))
    {
        m_relativeURI += "#";
        m_relativeURI.append(m_URL.c_str() + m_parsedURL.field_data[UF_FRAGMENT].off, m_parsedURL.field_data[UF_FRAGMENT].len);
    }

    return true;
}

bool HttpClient::__writeRequestLine()
{
/*
    Request-Line = Method SP Request-URI SP HTTP-Version CRLF
    example: GET /foo/bar HTTP/1.1\r\n
*/
    bool retCode = false;
    switch (m_method)
    {
    case HTTP_METHOD_GET:
        retCode = __writeRequest("GET ", 4);
        break;
    case HTTP_METHOD_HEAD:
        retCode = __writeRequest("HEAD ", 5);
        break;
    case HTTP_METHOD_POST:
        retCode = __writeRequest("POST ", 5);
        break;
    default:
        assert(!"unknown method");
    }
    if (!retCode)
        return false;
    if (!__writeRequest(m_relativeURI.c_str(), (unsigned int)m_relativeURI.length()))
        return false;
    if (!__writeRequest(" HTTP/1.1\r\n", 11))
        return false;

/*
    Host = "Host" ":" host [ ":" port ] ; Section 3.2.2

    A client MUST include a Host header field in all HTTP/1.1 request messages;
    All Internet-based HTTP/1.1 servers MUST respond with a 400 (Bad Request) status code 
      to any HTTP/1.1 request message which lacks a Host header field. 
*/
    if (!__writeRequest("Host: ", 6))
        return false;
    if (!__writeRequest(m_host.c_str(), (unsigned int)m_host.length()))
        return false;
    if (m_port != 80)
    {
        char str[11];
        str[0] = ':';
        _itoa(m_port, str + 1, 10);
        if (!__writeRequest(str, (unsigned int)strlen(str)))
            return false;
    }
    if (!__writeRequest("\r\n", 2))
        return false;

    return true;
}

bool HttpClient::__writeRequest(const void *data, unsigned int len)
{
    if (m_requestSize + len <= m_requestMaxSize)
    {
        memcpy(m_request + m_requestSize, data, len);
        m_requestSize += len;
        return true;
    }
    return false;
}

void HttpClient::__reset()
{
    m_requestSize = 0;

    m_method = (HTTP_METHOD)0;
    m_URL.clear();
    memset(&m_parsedURL, 0, sizeof(m_parsedURL));
    m_host.clear();
    m_port = 0;
    m_relativeURI.clear();

    memset(&m_parser, 0, sizeof(m_parser));
    m_headerField.c_str();
    m_headerValue.c_str();
    m_lastHeaderCBType = none;
    m_isStatusNotified = false;
    m_isMessageComplete = false;

    m_useNewConn = false;
    m_conn = NULL;
    m_isNewConn = false;
    m_requestSent = 0;
    m_responseReceived = 0;

    m_rateTimer = NULL;
    m_rateTokens = 0;
}

//------------------------------------------------------------------------------

void HttpClient::__onDone(bool isError)
{
    if (m_rateTimer)
    {
        nanoev_event_free(m_rateTimer);
        m_rateTimer = NULL;
    }

    if (isError)
    {
        nanoev_event_free(m_conn);
        m_conn = NULL;
    }
    else
    {
        m_connPool->Put(m_host, m_port, m_conn);
        m_conn = NULL;
    }

    m_stage = done;

    if (isError)
        m_handler->onError();
    else
        m_handler->onEnd();
}

//------------------------------------------------------------------------------

void HttpClient::__start(nanoev_loop *loop, bool useConnPool)
{
    assert(m_conn == NULL);

    if (m_rateLimiter != NULL && m_rateTimer == NULL)
    {
        m_rateTimer = nanoev_event_new(nanoev_event_timer, loop, this);
    }

    if (useConnPool)
    {
        m_conn = m_connPool->Get(m_host, m_port);
    }

    if (m_conn == NULL)
    {
        // 新建连接
        m_conn = nanoev_event_new(nanoev_event_tcp, loop, this);
        m_isNewConn = true;

        // resolve DNS
        struct hostent* h = gethostbyname(m_host.c_str());
        if (!h)
        {
            __onDone(true);
            return;
        }
        struct in_addr addr;
        addr.s_addr = *(u_long *)h->h_addr;

        // connect
        struct nanoev_addr server_addr;
        nanoev_addr_init(&server_addr, inet_ntoa(addr), m_port);
        int res = nanoev_tcp_connect(m_conn, &server_addr, __onConnect);
        if (res != NANOEV_SUCCESS)
        {
            __onDone(true);
            return;
        }
    }
    else
    {
        // 从连接池中取得了已有连接
        assert(!m_isNewConn);
        
        nanoev_event_set_userdata(m_conn, this);
        
        // 直接开始发送request
        int res = nanoev_tcp_write(m_conn, m_request, m_requestSize, __onWrite);
        if (res != NANOEV_SUCCESS)
        {
            __onDone(true);
            return;
        }
    }
}

void HttpClient::__onConnect(int status)
{
    if (status)
    {
        __onDone(true);
        return;
    }

    int res = nanoev_tcp_write(m_conn, m_request, m_requestSize, __onWrite);
    if (res != NANOEV_SUCCESS)
    {
        __onDone(true);
        return;
    }
}

void HttpClient::__onWrite(int status, void *buf, unsigned int bytes)
{
    if (status)
    {
        if (m_isNewConn)
        {
            __onDone(true);
            return;
        }
        else
        {
            // 复用连接池中的连接，然后出错了
            nanoev_loop *loop = nanoev_event_loop(m_conn);

            nanoev_event_free(m_conn);
            m_conn = NULL;
            m_requestSent = 0;

            // 禁用连接池，重试一次
            __start(loop, false);
            return;
        }
    }

    m_requestSent += bytes;
    
    size_t remains = m_requestSize - m_requestSent;
    if (remains)
    {
        // 继续发送
        int res = nanoev_tcp_write(m_conn, m_request + m_requestSent, (unsigned int)remains, __onWrite);
        if (res != NANOEV_SUCCESS)
        {
            __onDone(true);
            return;
        }
    }
    else
    {
        // 开始接收
        unsigned int toRecv = __prepareReadTokens();
        if (!toRecv)
            return;

        int res = nanoev_tcp_read(m_conn, m_recvBuf, toRecv, __onRead);
        if (res != NANOEV_SUCCESS)
        {
            __onDone(true);
            return;
        }
    }
}

void HttpClient::__onRead(int status, void *buf, unsigned int bytes)
{
    if (status)
    {
        __onDone(true);
        return;
    }

    size_t nparsed = http_parser_execute(&m_parser, &m_parserSettings, (const char*)buf, bytes);
    if (nparsed != (size_t)bytes)
    {
        __onDone(true);
        return;
    }

    __consumeReadTokens(bytes);

    m_responseReceived += bytes;

    if (!m_isMessageComplete)
    {
        if (bytes > 0)
        {
            unsigned int toRecv = __prepareReadTokens();
            if (!toRecv)
                return;

            int res = nanoev_tcp_read(m_conn, m_recvBuf, toRecv, __onRead);
            if (res != NANOEV_SUCCESS)
            {
                __onDone(true);
                return;
            }
        }
        else
        {
            if (!m_responseReceived && !m_isNewConn)
            {
                // 连接断开了；尚未收到任何response数据；是复用的连接池中的连接
                nanoev_loop *loop = nanoev_event_loop(m_conn);

                nanoev_event_free(m_conn);
                m_conn = NULL;
                m_requestSent = 0;

                // 禁用连接池，重试一次
                __start(loop, false);
                return;
            }

            __onDone(true);
            return;
        }
    }
    else
    {
        // 正常读完了整个response
        __onDone(false);
        return;
    }
}

unsigned int HttpClient::__prepareReadTokens()
{
    if (m_rateLimiter == NULL)
        return m_recvBufSize;

    if (m_rateTokens < 100)
    {
        m_rateTokens += m_rateLimiter->Take(m_recvBufSize);
        if (m_rateTokens < 100)
        {
            int ms = m_rateLimiter->WaitHint();
            struct nanoev_timeval after = { ms / 1000, (ms % 1000)*1000 };
            nanoev_timer_add(m_rateTimer, after, 0, __onTimer);
            return 0;
        }
    }
    return m_rateTokens;
}

void HttpClient::__consumeReadTokens(unsigned int n)
{
    if (m_rateLimiter == NULL)
        return;

    assert(m_rateTokens >= n);
    m_rateTokens -= n;
}

void HttpClient::__onRateTimer()
{
    unsigned int toRecv = __prepareReadTokens();
    if (!toRecv)
        return;

    int res = nanoev_tcp_read(m_conn, m_recvBuf, toRecv, __onRead);
    if (res != NANOEV_SUCCESS)
    {
        __onDone(true);
        return;
    }
}

void HttpClient::__start(nanoev_loop *loop, void *ctx)
{
    HttpClient *self = (HttpClient*)ctx;
    self->__start(loop, !self->m_useNewConn);
}

void HttpClient::__onConnect(nanoev_event *tcp, int status)
{
    HttpClient *self = (HttpClient*)nanoev_event_userdata(tcp);
    assert(self->m_conn == tcp);
    self->__onConnect(status);
}

void HttpClient::__onWrite(nanoev_event *tcp, int status, void *buf, unsigned int bytes)
{
    HttpClient *self = (HttpClient*)nanoev_event_userdata(tcp);
    assert(self->m_conn == tcp);
    self->__onWrite(status, buf, bytes);
}

void HttpClient::__onRead(nanoev_event *tcp, int status, void *buf, unsigned int bytes)
{
    HttpClient *self = (HttpClient*)nanoev_event_userdata(tcp);
    assert(self->m_conn == tcp);
    self->__onRead(status, buf, bytes);
}

void HttpClient::__onTimer(nanoev_event *timer)
{
    HttpClient *self = (HttpClient*)nanoev_event_userdata(timer);
    assert(self->m_rateTimer == timer);
    self->__onRateTimer();
}

//------------------------------------------------------------------------------

int HttpClient::__onHeaderField(http_parser *parser, const char *at, size_t length)
{
    HttpClient *self = (HttpClient*)parser->data;

    if (!self->m_isStatusNotified)
    {
        self->m_isStatusNotified = true;
        self->m_handler->onStatus(parser->status_code);
    }

    if (self->m_lastHeaderCBType == value)
    {
        self->m_handler->onHeader(
            self->m_headerField.c_str(), (unsigned int)self->m_headerField.length(), 
            self->m_headerValue.c_str(), (unsigned int)self->m_headerValue.length()
            );

        self->m_headerField.clear();
        self->m_headerValue.clear();
    }

    self->m_headerField.append(at, length);
    self->m_lastHeaderCBType = field;

    return 0;
}

int HttpClient::__onHeaderValue(http_parser *parser, const char *at, size_t length)
{
    HttpClient *self = (HttpClient*)parser->data;

    self->m_headerValue.append(at, length);
    self->m_lastHeaderCBType = value;
    
    return 0;
}

int HttpClient::__onHeadersComplete(http_parser *parser)
{
    HttpClient *self = (HttpClient*)parser->data;

    if (!self->m_isStatusNotified)
    {
        self->m_isStatusNotified = true;
        self->m_handler->onStatus(parser->status_code);
    }

    if (self->m_headerField.length())
    {
        self->m_handler->onHeader(
            self->m_headerField.c_str(), (unsigned int)self->m_headerField.length(), 
            self->m_headerValue.c_str(), (unsigned int)self->m_headerValue.length()
            );

        self->m_headerField.clear();
        self->m_headerValue.clear();
    }

    self->m_handler->onHeaderEnd();

    if (self->m_method == HTTP_METHOD_HEAD) // HEAD请求的Response中没有body
    {
        __onMessageComplete(parser);
    }

    return 0;
}

int HttpClient::__onBody(http_parser *parser, const char *at, size_t length)
{
    HttpClient *self = (HttpClient*)parser->data;

    self->m_handler->onBody(at, (unsigned int)length);

    return 0;
}

int HttpClient::__onMessageComplete(http_parser *parser)
{
    HttpClient *self = (HttpClient*)parser->data;
    
    self->m_isMessageComplete = true;
    
    return 0;
}
