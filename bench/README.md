# nanoev Bench

`nanoev_bench` is a small load generator for exercising nanoev under many
TCP connections. The command line reserves a protocol selector so UDP can be
added later, but the first implementation supports TCP only.

Build with benchmarks enabled:

```sh
cmake -S . -B build -DNANOEV_BUILD_BENCHMARKS=ON
cmake --build build
```

Run a TCP server:

```sh
./build/nanoev_bench --protocol tcp --role server --host 127.0.0.1 --port 4000
```

Run a TCP client:

```sh
./build/nanoev_bench --protocol tcp --role client --host 127.0.0.1 --port 4000 --connections 100 --message-size 64 --duration 30
```

Useful options:

- `--connections COUNT`: number of client connections.
- `--message-size BYTES`: payload bytes per request frame.
- `--duration SECONDS`: client run duration.
- `--report-interval SECONDS`: periodic stats interval.
- `--backlog COUNT`: TCP listen backlog for the server.
- `--ipv6`: use `::1` and IPv6.
- `--pipeline DEPTH`: reserved for future pipelined clients. It must be `1`
  for now because nanoev currently allows one pending read and one pending write
  per event.

High connection counts require enough file descriptors for both the client and
server processes. On systems with a low default limit, check `ulimit -n` and
raise it before running large connection counts.

The client reports total request throughput, transferred MiB, error count, and
approximate latency percentiles. The percentile values come from a power-of-two
microsecond histogram, so they are bucketed estimates rather than exact samples.
The server reports the same request counters and splits errors into accept-layer
and established-connection I/O errors.

Example client output:

```text
[client] interval stats
  time            req/s        MiB     requests          total     errors
  14:23:08     121.76k/s       8.36      121,764        121,764          0

[client] summary
  duration : 1.00s
  requests : 121,764 (121.76k/s)
  transfer : 8.36 MiB (8.36 MiB/s)
  errors   : 0
  latency  : min=10us avg=24us p50=32us p95=64us p99=128us max=204us
```
