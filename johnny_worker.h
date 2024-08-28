#ifndef JOHNNY_WORKER_H
#define JOHNNY_WORKER_H

#include <stdnoreturn.h>

noreturn void johnny_worker(int server_fd, int epfd);

#endif //JOHNNY_WORKER_H
