# Benchmark Performance Comparison

This document records a local TCP echo benchmark comparison between `nanoev_bench`
and `libevent_bench`. The libevent benchmark uses the conventional
`evconnlistener` + `bufferevent` API path.

## Environment

- Date: 2026-06-18
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
this run, all client and server summaries reported `errors=0`.

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
| 10 conn, 64B | 274.87k/s | 223.52k/s | +23.0% | 18.87 | 15.35 |
| 100 conn, 64B | 283.25k/s | 259.06k/s | +9.3% | 19.45 | 17.79 |
| 500 conn, 64B | 296.90k/s | 268.74k/s | +10.5% | 20.39 | 18.45 |
| 100 conn, 1024B | 250.46k/s | 231.18k/s | +8.3% | 246.50 | 227.53 |

## Latency

| Scenario | nanoev avg | nanoev p50 | nanoev p95 | nanoev p99 | libevent avg | libevent p50 | libevent p95 | libevent p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10 conn, 64B | 36us | 64us | 64us | 64us | 44us | 64us | 64us | 128us |
| 100 conn, 64B | 353us | 512us | 1024us | 1024us | 385us | 512us | 512us | 1024us |
| 500 conn, 64B | 1683us | 2048us | 4096us | 4096us | 1860us | 2048us | 2048us | 4096us |
| 100 conn, 1024B | 398us | 512us | 1024us | 1024us | 432us | 512us | 512us | 1024us |

## Run Spread

| Scenario | nanoev req/s runs | libevent req/s runs |
| --- | --- | --- |
| 10 conn, 64B | 275.41k/s, 274.87k/s, 265.67k/s | 223.52k/s, 224.32k/s, 205.65k/s |
| 100 conn, 64B | 286.49k/s, 283.25k/s, 277.08k/s | 258.22k/s, 259.06k/s, 260.26k/s |
| 500 conn, 64B | 298.97k/s, 265.46k/s, 296.90k/s | 251.88k/s, 268.74k/s, 269.53k/s |
| 100 conn, 1024B | 250.46k/s, 218.58k/s, 252.75k/s | 231.18k/s, 232.26k/s, 230.76k/s |

## Server Error Summary

| Scenario | nanoev server errors | libevent server errors |
| --- | ---: | ---: |
| 10 conn, 64B | 0, 0, 0 | 0, 0, 0 |
| 100 conn, 64B | 0, 0, 0 | 0, 0, 0 |
| 500 conn, 64B | 0, 0, 0 | 0, 0, 0 |
| 100 conn, 1024B | 0, 0, 0 | 0, 0, 0 |

## Interpretation

In this localhost echo benchmark, nanoev is ahead of the libevent bufferevent
implementation in all measured median results. The lead ranges from about 8% in
the 100 connection, 1024-byte case to about 23% in the 10 connection, 64-byte case.
Latency averages are also lower for nanoev in the median runs, while bucketed
tail percentiles are broadly similar.

This benchmark should not be read as a general claim that nanoev is faster than
libevent. It compares the current nanoev TCP benchmark implementation against a
common libevent bufferevent implementation under one machine, one OS, one
transport, and one request pattern. For stronger claims, add CPU usage, Linux
epoll measurements, additional payload and pipeline shapes, and repeated runs
on otherwise idle systems.
