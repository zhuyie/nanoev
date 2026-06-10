# nanoev

nanoev is a small async event library for TCP, UDP, timers, and cross-thread
loop notifications. It provides a compact C API with a single-threaded event
loop and platform-specific polling backends.

## Features

- TCP connect, listen, accept, read, and write
- UDP bind, connect, read, and write
- Async DNS resolution
- One-shot and repeating timers
- Cross-thread loop wakeups with async events
- IPv4 and IPv6 address helpers
- C API with a C++ include wrapper

## Platforms

| Platform | Backend |
| --- | --- |
| Windows | IOCP |
| macOS | kqueue |
| Linux | epoll |

## Build

nanoev uses CMake.

```sh
cmake -S . -B build
cmake --build build
```

The default build creates the static `nanoev` library and the example test
programs:

- `udp_test`
- `test_client`
- `test_server`

To build only the library:

```sh
cmake -S . -B build -DNANOEV_BUILD_TESTS=OFF
cmake --build build
```

To install nanoev and consume it from another CMake project:

```sh
cmake --install build --prefix /path/to/prefix
```

```cmake
find_package(nanoev CONFIG REQUIRED)
target_link_libraries(app PRIVATE nanoev::nanoev)
```

On Windows, use a Visual Studio generator or run CMake from a Developer Command
Prompt:

```bat
cmake -S . -B build
cmake --build build --config Release
```

## Run The Examples

Run the UDP loopback test:

```sh
./build/udp_test
```

Run the TCP echo server and client in separate terminals:

```sh
./build/test_server
```

```sh
./build/test_client
```

Pass `-ipv6` to the example programs to use `::1` instead of `127.0.0.1`.

## API Overview

Applications initialize nanoev, create a loop, attach events to that loop, and
then run the loop until `nanoev_loop_break()` is called.

```c
#include "nanoev.h"
#include <stdio.h>

static void on_timer(nanoev_event *timer)
{
    /* Timer callbacks run on the loop thread. */
    printf("timer fired\n");
    nanoev_loop_break(nanoev_event_loop(timer));
}

int main(void)
{
    /* Initialize platform state. Windows uses this to start Winsock. */
    nanoev_init();

    /* Create one event loop and attach a one-shot timer to it. */
    nanoev_loop *loop = nanoev_loop_new(NULL);
    nanoev_event *timer = nanoev_event_new(nanoev_event_timer, loop, NULL);

    /* Fire the timer after one second. */
    nanoev_timeval after = { 1, 0 };
    nanoev_timer_add(timer, after, 0, on_timer);

    /* Run callbacks until the timer calls nanoev_loop_break(). */
    nanoev_loop_run(loop);

    /* Free events before freeing their loop. */
    nanoev_event_free(timer);
    nanoev_loop_free(loop);

    /* Terminate platform state initialized by nanoev_init(). */
    nanoev_term();

    return 0;
}
```

Most operations are asynchronous. Completion is reported through the callback
passed to the operation.

## Usage Notes

- Events belong to the loop that created them.
- Event operations are expected to run on the loop thread, except
  `nanoev_async_send()`, which may be used to wake the loop from another thread.
- TCP and UDP keep the API simple: schedule at most one pending read and one
  pending write on an event at a time.
- TCP connect, accept, read, and write operations may take a timeout. When a
  TCP operation times out, its callback receives the platform socket timeout
  error and the TCP event enters the error state.
- UDP may be connected to a default peer, allowing writes with a `NULL`
  destination address and peer-filtered reads according to platform socket
  semantics.
- DNS resolution uses the system resolver on a fixed worker pool and reports
  completion on the loop thread. Freeing a DNS event with a pending resolve
  cancels the callback, but the worker may continue until the system resolver
  returns.
- TCP reads and writes may complete with fewer bytes than requested. Callers
  should continue reading or writing in their callbacks when they need a full
  message.
- A TCP read completion with `bytes == 0` means the peer closed the connection.
- Async events coalesce notifications: multiple sends before the loop handles
  them may result in a single callback.

## Headers

Use `include/nanoev.h` from C code:

```c
#include "nanoev.h"
```

Use `include/nanoev.hpp` from C++ code:

```cpp
#include "nanoev.hpp"
```
