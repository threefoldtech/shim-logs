#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include "shim-logs.h"

// epoll_wait timeout
#define EVTIMEOUT 1000

// global logfile (copy of stdout/stderr)
FILE *syslogfile = NULL;

void diep(char *str) {
    errlog("[-] %s: %s\n", str, strerror(errno));
    exit(EXIT_FAILURE);
}

void attach_localfile(container_t *container) {
    char path[512];

    // we don't check if it works, let assume
    // contd is running
    mkdir(LOGSDIR, 0775);

    sprintf(path, "%s/%s", LOGSDIR, container->namespace);
    mkdir(path, 0775);

    sprintf(path, "%s/%s/%s.log", LOGSDIR, container->namespace, container->id);

    // do not crash if we can't open it, silently ignore it
    file_t *local;
    if(!(local = file_new(path)))
        return;

    // attaching stdout and stderr to the same file
    log_attach(container->logout, local, local->write);
    log_attach(container->logerr, local, local->write);
}

void syslogfile_init(container_t *container) {
    char path[512];

    sprintf(path, "%s/%s/%s-sys.log", LOGSDIR, container->namespace, container->id);

    // fallback to stdout if open failed, to not crash
    if(!(syslogfile = fopen(path, "a"))) {
        perror(path);
        syslogfile = stdout;
    }
}

int main() {
    printf("[+] initializing shim-logs v" SHIMLOGS_VERSION "\n");
    syslogfile = stdout;

    //
    // container object
    //
    container_t *container;

    if(!(container = container_init()))
        diep("container");

    syslogfile_init(container);

    if(!(container_load(container))) {
        errlog("[-] could not load configuration\n");
        exit(EXIT_FAILURE);
    }

    //
    // debug file backend
    //
    attach_localfile(container);

    //
    // initialize async
    //
    struct epoll_event event;
    struct epoll_event *events = NULL;
    int evfd;

    memset(&event, 0, sizeof(struct epoll_event));

    if((evfd = epoll_create1(0)) < 0)
        diep("epoll_create1");

    event.data.fd = 3;
    event.events = EPOLLIN;

    if(epoll_ctl(evfd, EPOLL_CTL_ADD, 3, &event) < 0)
        diep("epoll_ctl");

    event.data.fd = 4;
    event.events = EPOLLIN;

    if(epoll_ctl(evfd, EPOLL_CTL_ADD, 4, &event) < 0)
        diep("epoll_ctl");

    if(!(events = calloc(MAXEVENTS, sizeof(event))))
        diep("calloc");

    //
    // notify caller we are ready
    //
    container_ready(container);

    //
    // async fetching logs
    //
    while(1) {
        int n = epoll_wait(evfd, events, MAXEVENTS, EVTIMEOUT);

        if(n < 0) {
            if(errno == EINTR)
                continue;

            diep("epoll_wait");
        }

        for(int i = 0; i < n; i++) {
            struct epoll_event *ev = events + i;

            if(ev->events & EPOLLIN) {
                log_t *target = NULL;

                if(ev->data.fd == container->logout->fd)
                    target = container->logout;

                if(ev->data.fd == container->logerr->fd)
                    target = container->logerr;

                // printf("[+] reading fd: %d\n", target->fd);
                stream_read(target->fd, target->stream);

                char *line;
                while((line = stream_line(target->stream)))
                    log_dispatch(target, line);

                if(stream_remain(target->stream) == 0) {
                    // printf("[+] recall stream buffer\n");
                    stream_recall(target->stream);
                }
            }

            if(ev->events & EPOLLERR || ev->events & EPOLLHUP) {
                log("[-] file descriptor closed\n");
                return 1;
            }
        }
    }

    return 0;
}
