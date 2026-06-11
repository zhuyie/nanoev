# Benchmark Performance Comparison

This document records a local TCP echo benchmark comparison between `nanoev_bench`
and `libevent_bench`. The libevent benchmark uses the conventional
`evconnlistener` + `bufferevent` API path.

## Environment

- Date: 2026-06-11
- Host: Apple M2 Max, macOS Darwin 25.5.0 arm64
- File descriptor limit: `1048575`
- Build type: `Release`
- Transport: TCP over `127.0.0.1`
- Benchmark shape: one in-flight request per connection, fixed frame echo
- Runner: `bench/run_compare.sh` from a user terminal

Build command:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DNANOEV_BUILD_BENCHMARKS=ON
cmake --build build-release
```

## Method

Each scenario was run 3 times for 15 seconds, and the table reports the median
run by client-side request throughput. The client-side summary is the primary
throughput and latency signal because the server was started before the client
and stopped manually after the client exited.

Server error summaries are still recorded to catch accept or I/O errors. In
this run, all client summaries reported `errors=0`. nanoev server summaries
reported a few accept errors under higher connection pressure; libevent server
summaries reported none. The nanoev accept errors were `accept` category only,
with `io=0`, and did not correspond to failed client requests in this run.

## Runner

The recorded run used the comparison script:

```sh
./bench/run_compare.sh
```

The script starts and stops the server for each run, sleeps between runs, and
writes raw logs plus TSV summaries under `bench/results/`.

## Results

| Scenario | nanoev req/s | libevent req/s | Delta | nanoev MiB/s | libevent MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10 conn, 64B | 275.49k/s | 221.56k/s | +24.3% | 18.92 | 15.21 |
| 100 conn, 64B | 283.00k/s | 255.35k/s | +10.8% | 19.43 | 17.53 |
| 500 conn, 64B | 298.59k/s | 249.54k/s | +19.7% | 20.50 | 17.13 |
| 100 conn, 1024B | 247.82k/s | 221.32k/s | +12.0% | 243.90 | 217.82 |

## Latency

| Scenario | nanoev avg | nanoev p50 | nanoev p95 | nanoev p99 | libevent avg | libevent p50 | libevent p95 | libevent p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10 conn, 64B | 29us | 32us | 64us | 64us | 45us | 64us | 64us | 128us |
| 100 conn, 64B | 349us | 512us | 512us | 1024us | 391us | 512us | 512us | 1024us |
| 500 conn, 64B | 1542us | 2048us | 4096us | 4096us | 2002us | 2048us | 4096us | 8192us |
| 100 conn, 1024B | 350us | 512us | 512us | 1024us | 451us | 512us | 1024us | 1024us |

## Run Spread

| Scenario | nanoev req/s runs | libevent req/s runs |
| --- | --- | --- |
| 10 conn, 64B | 274.46k/s, 278.17k/s, 275.49k/s | 221.88k/s, 220.84k/s, 221.56k/s |
| 100 conn, 64B | 283.00k/s, 280.78k/s, 285.68k/s | 256.41k/s, 253.26k/s, 255.35k/s |
| 500 conn, 64B | 297.34k/s, 298.59k/s, 299.03k/s | 247.38k/s, 249.54k/s, 250.63k/s |
| 100 conn, 1024B | 249.07k/s, 247.82k/s, 245.28k/s | 221.32k/s, 222.11k/s, 221.19k/s |

## Server Error Summary

| Scenario | nanoev server errors | libevent server errors |
| --- | ---: | ---: |
| 10 conn, 64B | 2, 1, 2 | 0, 0, 0 |
| 100 conn, 64B | 1, 0, 0 | 0, 0, 0 |
| 500 conn, 64B | 47, 39, 26 | 0, 0, 0 |
| 100 conn, 1024B | 2, 13, 10 | 0, 0, 0 |

## Interpretation

In this localhost echo benchmark, nanoev is ahead of the libevent bufferevent
implementation in all measured median results. The lead ranges from about 11% in
the 100 connection, 64-byte case to about 24% in the 10 connection, 64-byte case.
Latency averages are also lower for nanoev in the median runs, while bucketed
tail percentiles are broadly similar.

This benchmark should not be read as a general claim that nanoev is faster than
libevent. It compares the current nanoev TCP benchmark implementation against a
common libevent bufferevent implementation under one machine, one OS, one
transport, and one request pattern. For stronger claims, add CPU usage, Linux
epoll measurements, additional payload and pipeline shapes, and repeated runs
on otherwise idle systems.
