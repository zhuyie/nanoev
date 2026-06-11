# Benchmark Performance Comparison

This document records a local TCP echo benchmark comparison between `nanoev_bench`
and `libevent_bench`. The libevent benchmark uses the conventional
`evconnlistener` + `bufferevent` API path.

## Environment

- Date: 2026-06-11
- Commit: `8e6c04b`
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
| 10 conn, 64B | 261.99k/s | 223.64k/s | +17.1% | 17.99 | 15.36 |
| 100 conn, 64B | 273.70k/s | 256.65k/s | +6.6% | 18.79 | 17.62 |
| 500 conn, 64B | 293.85k/s | 266.55k/s | +10.2% | 20.18 | 18.30 |
| 100 conn, 1024B | 246.16k/s | 227.94k/s | +8.0% | 242.27 | 224.34 |

## Latency

| Scenario | nanoev avg | nanoev p50 | nanoev p95 | nanoev p99 | libevent avg | libevent p50 | libevent p95 | libevent p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10 conn, 64B | 26us | 32us | 64us | 64us | 44us | 64us | 64us | 128us |
| 100 conn, 64B | 358us | 512us | 1024us | 1024us | 389us | 512us | 512us | 1024us |
| 500 conn, 64B | 1606us | 2048us | 2048us | 4096us | 1875us | 2048us | 2048us | 4096us |
| 100 conn, 1024B | 393us | 512us | 1024us | 1024us | 438us | 512us | 512us | 1024us |

## Run Spread

| Scenario | nanoev req/s runs | libevent req/s runs |
| --- | --- | --- |
| 10 conn, 64B | 261.99k/s, 276.69k/s, 261.71k/s | 223.64k/s, 222.33k/s, 223.91k/s |
| 100 conn, 64B | 273.70k/s, 259.07k/s, 286.74k/s | 256.44k/s, 256.65k/s, 257.67k/s |
| 500 conn, 64B | 297.17k/s, 286.43k/s, 293.85k/s | 266.55k/s, 264.70k/s, 267.39k/s |
| 100 conn, 1024B | 246.16k/s, 242.70k/s, 248.23k/s | 228.68k/s, 216.16k/s, 227.94k/s |

## Server Error Summary

| Scenario | nanoev server errors | libevent server errors |
| --- | ---: | ---: |
| 10 conn, 64B | 3, 1, 3 | 0, 0, 0 |
| 100 conn, 64B | 2, 3, 18 | 0, 0, 0 |
| 500 conn, 64B | 27, 21, 28 | 0, 0, 0 |
| 100 conn, 1024B | 3, 10, 6 | 0, 0, 0 |

## Interpretation

In this localhost echo benchmark, nanoev is ahead of the libevent bufferevent
implementation in all measured median results. The lead ranges from about 7% in
the 100 connection, 64-byte case to about 17% in the 10 connection, 64-byte case.
Latency averages are also lower for nanoev in the median runs, while bucketed
tail percentiles are broadly similar.

This benchmark should not be read as a general claim that nanoev is faster than
libevent. It compares the current nanoev TCP benchmark implementation against a
common libevent bufferevent implementation under one machine, one OS, one
transport, and one request pattern. For stronger claims, add CPU usage, Linux
epoll measurements, additional payload and pipeline shapes, and repeated runs
on otherwise idle systems.
