# nanoev

nanoev is a small async event library for TCP, UDP, timers, and cross-thread
loop notifications. It provides a compact C API with a single-threaded event
loop and platform-specific polling backends.

## Features

- TCP connect, listen, accept, read, and write
- UDP bind, read, and write
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
nanoev_init();

nanoev_loop *loop = nanoev_loop_new(NULL);
nanoev_event *timer = nanoev_event_new(nanoev_event_timer, loop, NULL);

nanoev_timeval after = { 1, 0 };
nanoev_timer_add(timer, after, 0, on_timer);

nanoev_loop_run(loop);

nanoev_event_free(timer);
nanoev_loop_free(loop);
nanoev_term();
```

Most operations are asynchronous. Completion is reported through the callback
passed to the operation.

## Usage Notes

- Events belong to the loop that created them.
- Event operations are expected to run on the loop thread, except
  `nanoev_async_send()`, which may be used to wake the loop from another thread.
- TCP and UDP keep the API simple: schedule at most one pending read and one
  pending write on an event at a time.
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
