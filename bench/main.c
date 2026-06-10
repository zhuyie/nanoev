#include "tcp.h"
#include "nanoev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    printf("Usage:\n");
    printf("  %s --protocol tcp --role server [options]\n", program);
    printf("  %s --protocol tcp --role client [options]\n", program);
    printf("\nOptions:\n");
    printf("  --protocol tcp          Benchmark protocol. UDP is reserved for later.\n");
    printf("  --role server|client    Benchmark role.\n");
    printf("  --host HOST             Bind or connect host. Default: 127.0.0.1.\n");
    printf("  --port PORT             Bind or connect port. Default: 4000.\n");
    printf("  --ipv6                  Use ::1 and IPv6 address family.\n");
    printf("  --duration SECONDS      Client run duration. Default: 10.\n");
    printf("  --connections COUNT     Client connection count. Default: 1.\n");
    printf("  --message-size BYTES    Frame payload bytes. Default: 64.\n");
    printf("  --pipeline DEPTH        Parsed for future use. Currently must be 1.\n");
    printf("  --backlog COUNT         Server listen backlog. Default: 1024.\n");
    printf("  --report-interval SEC   Periodic report interval. Default: 1.\n");
}

static int parse_uint(const char *value, unsigned int *out)
{
    char *end;
    unsigned long parsed;

    if (!value || !*value)
        return -1;
    parsed = strtoul(value, &end, 10);
    if (*end != '\0' || parsed > 0xffffffffUL)
        return -1;
    *out = (unsigned int)parsed;
    return 0;
}

static int next_arg(int argc, char **argv, int *index, const char **value)
{
    if (*index + 1 >= argc)
        return -1;
    (*index)++;
    *value = argv[*index];
    return 0;
}

int main(int argc, char **argv)
{
    bench_config config;
    const char *protocol = "tcp";
    int role_set = 0;
    int i;
    int ret;

    config.role = bench_role_client;
    config.host = "127.0.0.1";
    config.port = 4000;
    config.family = NANOEV_AF_INET;
    config.duration = 10;
    config.connections = 1;
    config.message_size = 64;
    config.pipeline = 1;
    config.backlog = 1024;
    config.report_interval = 1;

    for (i = 1; i < argc; i++) {
        const char *value;

        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--protocol") == 0) {
            if (next_arg(argc, argv, &i, &protocol))
                goto invalid_arg;
        } else if (strcmp(argv[i], "--role") == 0) {
            if (next_arg(argc, argv, &i, &value))
                goto invalid_arg;
            if (strcmp(value, "server") == 0) {
                config.role = bench_role_server;
                role_set = 1;
            } else if (strcmp(value, "client") == 0) {
                config.role = bench_role_client;
                role_set = 1;
            } else {
                goto invalid_arg;
            }
        } else if (strcmp(argv[i], "--host") == 0) {
            if (next_arg(argc, argv, &i, &config.host))
                goto invalid_arg;
        } else if (strcmp(argv[i], "--port") == 0) {
            unsigned int port;
            if (next_arg(argc, argv, &i, &value) || parse_uint(value, &port) || port > 65535)
                goto invalid_arg;
            config.port = (unsigned short)port;
        } else if (strcmp(argv[i], "--ipv6") == 0) {
            config.family = NANOEV_AF_INET6;
            config.host = "::1";
        } else if (strcmp(argv[i], "--duration") == 0) {
            if (next_arg(argc, argv, &i, &value) || parse_uint(value, &config.duration))
                goto invalid_arg;
        } else if (strcmp(argv[i], "--connections") == 0) {
            if (next_arg(argc, argv, &i, &value) || parse_uint(value, &config.connections))
                goto invalid_arg;
        } else if (strcmp(argv[i], "--message-size") == 0) {
            if (next_arg(argc, argv, &i, &value) || parse_uint(value, &config.message_size))
                goto invalid_arg;
        } else if (strcmp(argv[i], "--pipeline") == 0) {
            if (next_arg(argc, argv, &i, &value) || parse_uint(value, &config.pipeline))
                goto invalid_arg;
        } else if (strcmp(argv[i], "--backlog") == 0) {
            if (next_arg(argc, argv, &i, &value) || parse_uint(value, &config.backlog))
                goto invalid_arg;
        } else if (strcmp(argv[i], "--report-interval") == 0) {
            if (next_arg(argc, argv, &i, &value) || parse_uint(value, &config.report_interval))
                goto invalid_arg;
        } else {
            goto invalid_arg;
        }
    }

    if (!role_set) {
        fprintf(stderr, "--role is required\n");
        usage(argv[0]);
        return 2;
    }
    if (strcmp(protocol, "tcp") != 0) {
        fprintf(stderr, "UDP benchmark is not implemented yet\n");
        return 2;
    }
    if (config.pipeline != 1) {
        fprintf(stderr, "--pipeline currently must be 1\n");
        return 2;
    }
    if (!config.duration || !config.connections || !config.message_size || !config.report_interval) {
        fprintf(stderr, "duration, connections, message-size, and report-interval must be non-zero\n");
        return 2;
    }

    if (config.role == bench_role_server)
        ret = bench_tcp_server_run(&config);
    else
        ret = bench_tcp_client_run(&config);
    if (ret != 0)
        fprintf(stderr, "benchmark failed\n");
    return ret;

invalid_arg:
    fprintf(stderr, "invalid argument near '%s'\n", argv[i]);
    usage(argv[0]);
    return 2;
}
