#ifndef __HTTP_RESPONSE_HANDLER_H__
#define __HTTP_RESPONSE_HANDLER_H__

class HttpResponseHandler
{
public:
    virtual ~HttpResponseHandler() {}

    // 出现错误时回调
    virtual void onError()
    {
    }

    // 状态行接收及解析完毕后回调
    virtual void onStatus(
        int code)
    {
    }

    // 一个HTTP Header接收及解析完毕后回调
    virtual void onHeader(
        const char *name, 
        unsigned int nameLen,
        const char *value,
        unsigned int valueLen)
    {
    }

    // Headers结束时回调
    virtual void onHeaderEnd()
    {
    }

    // 收到Body数据时回调
    virtual void onBody(
        const void *data,
        unsigned int len)
    {
    }

    // 整个Response结束时回调
    virtual void onEnd()
    {
    }
};

#endif // !__HTTP_RESPONSE_HANDLER_H__
