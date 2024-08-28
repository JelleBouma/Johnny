#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "johnny_setup.h"
#include "johnny_worker.h"

noreturn void start_johnny_worker() {
    johnny_worker(johnny_setup_server(), johnny_setup_epoll());
}

void set_thread_cpu(const pthread_t thread_id, const int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(thread_id, sizeof(cpu_set_t), &cpuset);
}

void create_johnny_worker_thread(const int core_id) {
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, start_johnny_worker, NULL);
    set_thread_cpu(thread_id, core_id);
    pthread_detach(thread_id);
}

noreturn void start_a_johnny_worker_on_each_core() {
    const int core_cnt = sysconf(_SC_NPROCESSORS_ONLN);
    for (int core_id = 1; core_id < core_cnt; core_id++)
        create_johnny_worker_thread(core_id);
    set_thread_cpu(pthread_self(), 0);
    start_johnny_worker();
}

int main() {

#ifndef NDEBUG // fix for console printing problems when running in debug
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

    if (johnny_setup_files())
        return EXIT_FAILURE;

    start_a_johnny_worker_on_each_core();
}
