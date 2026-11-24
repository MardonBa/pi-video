#include <stdio.h>

#define MAX_BUFFERS 8

struct buf_info {
    void *start;
    size_t length;
};

struct buf_fmt { // what we need from the v4l2_format
    int width;
    int height;
    int stride; // bytesperline
    int size; // sizeimage
};

typedef struct {
    int fd;
    struct buf_info mapped_buffers[MAX_BUFFERS];
    struct buf_fmt fmt;
    size_t mapped;
    bool streaming;
} camera_t;

camera_t *camera_open(const char *path, int width, int height);

void camera_close(camera_t *camera);
