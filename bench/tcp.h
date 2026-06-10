#ifndef NANOEV_BENCH_TCP_H
#define NANOEV_BENCH_TCP_H

typedef enum bench_role {
    bench_role_server = 0,
    bench_role_client
} bench_role;

typedef struct bench_config {
    bench_role role;
    const char *host;
    unsigned short port;
    int family;
    unsigned int duration;
    unsigned int connections;
    unsigned int message_size;
    unsigned int pipeline;
    unsigned int backlog;
    unsigned int report_interval;
} bench_config;

int bench_tcp_server_run(const bench_config *config);
int bench_tcp_client_run(const bench_config *config);

#endif
