#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "shim-logs.h"
#include "url_parser.h"

int redis_write(void *_self, char *line, int len) {
    (void) len;
    redis_t *self = (redis_t *) _self;
    redisReply *reply;

    if(!(reply = redisCommand(self->conn, "PUBLISH %s %s", self->channel, line))) {
        errlog("[-] redis error: %s\n", self->conn->errstr);
        return 1;
    }

    freeReplyObject(reply);

    return 0;
}

redis_t *redis_new(char *host, int port, char *channel, char *password) {
    redis_t *backend;
    redisReply *reply;
    struct timeval timeout = { 2, 0 };

    log("[+] redis backend: [%s:%d / %s]\n", host, port, channel);

    if(!(backend = calloc(sizeof(redis_t), 1)))
        diep("calloc");

    if(!(backend->conn = redisConnectWithTimeout(host, port, timeout))) {
        diep("redis");
        return NULL;
    }

    if(backend->conn->err) {
        errlog("[-] redis: %s\n", backend->conn->errstr);
        redisFree(backend->conn);
        free(backend);
        return NULL;
    }

    if(password) {
        if(!(reply = redisCommand(backend->conn, "AUTH %s", password)))
            diep("redis");

        if(reply->type == REDIS_REPLY_ERROR)
            log("redis: authentication failed\n");

        freeReplyObject(reply);
    }

    if(!(reply = redisCommand(backend->conn, "PING")))
        diep("redis");

    if(reply->type == REDIS_REPLY_ERROR)
        log("redis: could not access redis: %s\n", reply->str);

    freeReplyObject(reply);

    backend->channel = strdup(channel);
    backend->write = redis_write;

    return backend;
}

static int redis_attach(const char *url, log_t *target) {
    int port = 6379;
    struct parsed_url *purl;
    redis_t *redis;

    if(!(purl = parse_url(url)))
        return 1;

    if(purl->port)
        port = atoi(purl->port);

    if(!(redis = redis_new(purl->host, port, purl->path, purl->password)))
        return 1;

    parsed_url_free(purl);
    log_attach(target, redis, redis->write);

    return 0;
}

int redis_extract(container_t *c, json_t *root) {
    json_t *sout = json_object_get(root, "stdout");
    json_t *serr = json_object_get(root, "stderr");

    if(!json_is_string(sout) || !json_is_string(serr))
        return 1;

    redis_attach(json_string_value(sout), c->logout);
    redis_attach(json_string_value(serr), c->logerr);

    return 0;
}
