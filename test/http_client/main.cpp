// http_client.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "event_thread.h"

int main(int argc, char* argv[])
{
    EventThread eventThread;

    eventThread.start();

    Sleep(1000 * 30);

    eventThread.stop();

    return 0;
}

