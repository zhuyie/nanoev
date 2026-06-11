#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build-release"}
RESULT_DIR=${RESULT_DIR:-"$ROOT_DIR/bench/results"}
HOST=${HOST:-127.0.0.1}
BASE_PORT=${BASE_PORT:-4300}
DURATION=${DURATION:-15}
RUNS=${RUNS:-3}
SLEEP_AFTER_RUN=${SLEEP_AFTER_RUN:-5}
REPORT_INTERVAL=${REPORT_INTERVAL:-$DURATION}
SCENARIOS=${SCENARIOS:-"10:64 100:64 500:64 100:1024"}

NANOEV_BIN=$BUILD_DIR/nanoev_bench
LIBEVENT_BIN=$BUILD_DIR/libevent_bench

if [ ! -x "$NANOEV_BIN" ]; then
    echo "missing executable: $NANOEV_BIN" >&2
    echo "build with: cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DNANOEV_BUILD_BENCHMARKS=ON && cmake --build build-release" >&2
    exit 1
fi

if [ ! -x "$LIBEVENT_BIN" ]; then
    echo "missing executable: $LIBEVENT_BIN" >&2
    echo "libevent_bench is built only when libevent is found during CMake configure" >&2
    exit 1
fi

mkdir -p "$RESULT_DIR"
STAMP=$(date +%Y%m%d-%H%M%S)
LOG_FILE=$RESULT_DIR/compare-$STAMP.log
SUMMARY_FILE=$RESULT_DIR/compare-$STAMP-summary.tsv
MEDIAN_FILE=$RESULT_DIR/compare-$STAMP-median.tsv

cleanup_pid=
cleanup() {
    if [ -n "$cleanup_pid" ]; then
        kill -INT "$cleanup_pid" 2>/dev/null || true
        wait "$cleanup_pid" 2>/dev/null || true
    fi
}
trap cleanup INT TERM EXIT

run_one() {
    backend=$1
    bin=$2
    connections=$3
    message_size=$4
    run=$5
    port=$6

    {
        echo
        echo "=== backend=$backend connections=$connections message_size=$message_size run=$run port=$port ==="
        date "+started_at=%Y-%m-%d %H:%M:%S"
    } >> "$LOG_FILE"

    "$bin" --protocol tcp --role server \
        --host "$HOST" \
        --port "$port" \
        --message-size "$message_size" \
        --report-interval "$REPORT_INTERVAL" >> "$LOG_FILE" 2>&1 &
    cleanup_pid=$!

    sleep 2

    "$bin" --protocol tcp --role client \
        --host "$HOST" \
        --port "$port" \
        --connections "$connections" \
        --message-size "$message_size" \
        --duration "$DURATION" \
        --report-interval "$REPORT_INTERVAL" >> "$LOG_FILE" 2>&1

    kill -INT "$cleanup_pid" 2>/dev/null || true
    wait "$cleanup_pid" 2>/dev/null || true
    cleanup_pid=

    {
        date "+finished_at=%Y-%m-%d %H:%M:%S"
        echo "sleep_after_run=${SLEEP_AFTER_RUN}s"
    } >> "$LOG_FILE"
    sleep "$SLEEP_AFTER_RUN"
}

port=$BASE_PORT

{
    echo "log_file=$LOG_FILE"
    echo "summary_file=$SUMMARY_FILE"
    echo "median_file=$MEDIAN_FILE"
    echo "host=$HOST"
    echo "duration=$DURATION"
    echo "runs=$RUNS"
    echo "sleep_after_run=$SLEEP_AFTER_RUN"
    echo "scenarios=$SCENARIOS"
    echo
} | tee "$LOG_FILE"

for backend in nanoev libevent; do
    if [ "$backend" = nanoev ]; then
        bin=$NANOEV_BIN
    else
        bin=$LIBEVENT_BIN
    fi

    for scenario in $SCENARIOS; do
        connections=${scenario%:*}
        message_size=${scenario#*:}
        run=1
        while [ "$run" -le "$RUNS" ]; do
            port=$((port + 1))
            echo "running backend=$backend connections=$connections message_size=$message_size run=$run port=$port"
            run_one "$backend" "$bin" "$connections" "$message_size" "$run" "$port"
            run=$((run + 1))
        done
    done
done

awk '
BEGIN {
    print "backend\tconnections\tmessage_size\trun\trequests\treq_per_sec\tmib_per_sec\tclient_errors\tavg_us\tp50_us\tp95_us\tp99_us\tserver_errors\taccept_errors\tio_errors"
}
/^=== backend=/ {
    backend = connections = message_size = run = ""
    for (i = 1; i <= NF; i++) {
        split($i, pair, "=")
        if (pair[1] == "backend") backend = pair[2]
        if (pair[1] == "connections") connections = pair[2]
        if (pair[1] == "message_size") message_size = pair[2]
        if (pair[1] == "run") run = pair[2]
    }
}
/\[client\] summary/ { in_client = 1; next }
/\[server\] summary/ { in_client = 0; in_server = 1; next }
in_client && /requests :/ {
    requests = $3
    gsub(",", "", requests)
    req_per_sec = $4
    gsub("[()]", "", req_per_sec)
}
in_client && /transfer :/ {
    mib_per_sec = $5
    gsub("[()]", "", mib_per_sec)
}
in_client && /errors   :/ {
    client_errors = $3
    gsub(",", "", client_errors)
}
in_client && /latency  :/ {
    avg_us = p50_us = p95_us = p99_us = ""
    for (i = 1; i <= NF; i++) {
        if ($i ~ /^avg=/) { avg_us = $i; sub("avg=", "", avg_us); sub("us", "", avg_us) }
        if ($i ~ /^p50=/) { p50_us = $i; sub("p50=", "", p50_us); sub("us", "", p50_us) }
        if ($i ~ /^p95=/) { p95_us = $i; sub("p95=", "", p95_us); sub("us", "", p95_us) }
        if ($i ~ /^p99=/) { p99_us = $i; sub("p99=", "", p99_us); sub("us", "", p99_us) }
    }
}
in_server && /errors   :/ {
    server_errors = $3
    accept_errors = $4
    io_errors = $5
    gsub(",", "", server_errors)
    gsub("[(]accept=", "", accept_errors)
    gsub(",", "", accept_errors)
    gsub("io=", "", io_errors)
    gsub("[)]", "", io_errors)
    print backend "\t" connections "\t" message_size "\t" run "\t" requests "\t" req_per_sec "\t" mib_per_sec "\t" client_errors "\t" avg_us "\t" p50_us "\t" p95_us "\t" p99_us "\t" server_errors "\t" accept_errors "\t" io_errors
    in_server = 0
}
' "$LOG_FILE" > "$SUMMARY_FILE"

awk '
function rate_to_num(rate, value, suffix) {
    value = rate
    sub("/s$", "", value)
    suffix = substr(value, length(value), 1)
    if (suffix == "k") {
        sub("k$", "", value)
        return value * 1000
    }
    if (suffix == "M") {
        sub("M$", "", value)
        return value * 1000000
    }
    if (suffix == "G") {
        sub("G$", "", value)
        return value * 1000000000
    }
    return value + 0
}
BEGIN {
    FS = "\t"
    OFS = "\t"
    print "backend", "connections", "message_size", "source_run", "req_per_sec", "mib_per_sec", "client_errors", "avg_us", "p50_us", "p95_us", "p99_us", "server_errors", "accept_errors", "io_errors"
}
NR == 1 { next }
{
    key = $1 SUBSEP $2 SUBSEP $3
    count[key]++
    n = count[key]
    rate[key, n] = rate_to_num($6)
    line[key, n] = $0
}
END {
    for (key in count) {
        c = count[key]
        for (i = 1; i <= c; i++)
            order[i] = i
        for (i = 1; i <= c; i++) {
            for (j = i + 1; j <= c; j++) {
                if (rate[key, order[i]] > rate[key, order[j]]) {
                    tmp = order[i]
                    order[i] = order[j]
                    order[j] = tmp
                }
            }
        }
        median = order[int((c + 1) / 2)]
        split(line[key, median], fields, "\t")
        print fields[1], fields[2], fields[3], fields[4], fields[6], fields[7], fields[8], fields[9], fields[10], fields[11], fields[12], fields[13], fields[14], fields[15]
    }
}
' "$SUMMARY_FILE" > "$MEDIAN_FILE"

echo
echo "done"
echo "raw log: $LOG_FILE"
echo "summary: $SUMMARY_FILE"
echo "median: $MEDIAN_FILE"
