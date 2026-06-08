# nanoev

nanoev is a small async event library for TCP, UDP, timers, and cross-thread
loop notifications.

## Build

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
