#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "shim-logs.h"

stream_t *stream_new(int size) {
    stream_t *s;

    if(!(s = malloc(sizeof(stream_t))))
        return NULL;

    s->length = size;
    s->buffer = malloc(s->length);
    s->reader = s->buffer;
    s->writer = s->buffer;
    s->line = NULL;

    return s;
}

int stream_remain(stream_t *s) {
    return s->length - (s->writer - s->buffer);
}

int stream_read(int fd, stream_t *s) {
    int len;

    if((len = read(fd, s->writer, stream_remain(s))) < 0)
        diep("read");

    s->writer += len;
    *s->writer = '\0';

    return len;
}

char *stream_line(stream_t *s) {
    if(s->line == NULL)
        s->line = malloc(s->length);

    char *found = strchr(s->reader, '\n');
    if(!found) {
        if(stream_remain(s) == 0 && s->reader == s->buffer) {
            // there is no space left for new data
            // and the reader is at the begining of the buffer
            // this mean the buffer is full and there is no
            // new line found, we force flush
            found = s->writer - 1;

        } else {
            // no new line found yet, waiting more data
            return NULL;
        }
    }

    size_t length = found - s->reader + 1;

    strncpy(s->line, s->reader, length);
    s->line[length] = '\0';
    s->reader += length;

    return s->line;
}

void stream_recall(stream_t *s) {
    size_t length = s->length - (s->reader - s->buffer);

    memmove(s->buffer, s->reader, length);

    s->writer = s->buffer + length;
    s->reader = s->buffer;
    *s->writer = '\0';
}

