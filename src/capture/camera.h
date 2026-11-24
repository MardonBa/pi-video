#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

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

typedef enum {
    PIX_YUYV,
    PIX_GRAYSCALE,
    PIX_RGB
} pixel_format_t;

typedef struct {
    pixel_format_t pixel_format;
    int width;
    int height;
    int stride;
    uint8_t *data;
    size_t bytesize;
    uint32_t sequence;
    uint64_t timestamp;
    size_t capacity;
} frame_t;

camera_t *camera_open(const char *path, int width, int height);

void camera_close(camera_t *camera);

int camera_on(camera_t *camera);

int camera_off(camera_t *camera);

int camera_get_frame(camera_t *camera, frame_t *frame);
