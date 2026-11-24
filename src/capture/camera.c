#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <poll.h>
#include <stdint.h>
#include "camera.h"

// Helper for calling ioctl
static int xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);

    return r;
}

/*
 * returns a pointer to the struct that contains all the information
 * relevant to later functions, or NULL if there's a failure (main should
 * return 1 if NULL is returned by this function)
 */
camera_t *camera_open(const char *path, int width, int height) {

    camera_t *camera = calloc(1, sizeof(camera_t));

    if (camera == NULL) {
        return NULL;
    }

    camera->fd = -1; // default fd to be an invalid

    camera->fd = open(path, O_RDWR);
    if (camera->fd < 0) { // failed to open the device, fail quickly
        fprintf(stderr, "Failed to open camera (errno=%d): %s\n", errno, strerror(errno));
        camera_close(camera);
        return NULL;
    }

    // set format + framerate
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(camera->fd, VIDIOC_S_FMT, &fmt) == -1)  {
        perror("VIDIOC_S_FMT");
        camera_close(camera);
        return NULL;
    }

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        fprintf(stderr, "driver changed pixel format\n");
        camera_close(camera);
        return NULL;
    }

    printf("type:         %u (expected %u)\n", fmt.type, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    printf("width:        %u (expected %d)\n", fmt.fmt.pix.width, width);
    printf("height:       %u (expected %d)\n", fmt.fmt.pix.height, height);
    printf("pixelformat:  %c%c%c%c (expected YUYV)\n",
           fmt.fmt.pix.pixelformat & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
    printf("field:        %u (expected %u)\n", fmt.fmt.pix.field, V4L2_FIELD_NONE);
    printf("bytesperline: %u (expected 1280)\n", fmt.fmt.pix.bytesperline);
    printf("sizeimage:    %u (expected 614400)\n", fmt.fmt.pix.sizeimage);

    camera->fmt.width = fmt.fmt.pix.width;
    camera->fmt.height = fmt.fmt.pix.height;
    camera->fmt.stride = fmt.fmt.pix.bytesperline;
    camera->fmt.size = fmt.fmt.pix.sizeimage;

    struct v4l2_requestbuffers buffers = {0};
    buffers.count = 4;
    buffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffers.memory = V4L2_MEMORY_MMAP;

    if (xioctl(camera->fd, VIDIOC_REQBUFS, &buffers) == -1) {
        perror("VIDIOC_REQBUFS");
        camera_close(camera);
        return NULL;
    }

    if (buffers.count < 2 || buffers.count > MAX_BUFFERS) {
        fprintf(stderr, "unexpected buffer count: %u\n", buffers.count);
        camera_close(camera);
        return NULL;
    }

    printf("count:  %u (requested 4)\n", buffers.count);
    printf("type:   %u (expected %u)\n", buffers.type, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    printf("memory: %u (expected %u)\n", buffers.memory, V4L2_MEMORY_MMAP);

    // mapping + queuing buffers into use address space

    for (size_t i = 0; i < buffers.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = buffers.type;
        buf.memory = buffers.memory;
        buf.index = i;

        // mapping section

        if (xioctl(camera->fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("VIDIOC_QUERYBUF");
            camera_close(camera);
            return NULL;
        }

        void *p = mmap(
            NULL, // address, kernel will choose this
            buf.length, // size the driver reports for the buffer (from QUERY_BUF)
            PROT_READ | PROT_WRITE, // you can read and write
            MAP_SHARED, // share memory with driver, so you can see what the camera writes
            camera->fd, // what devices the buffers are from
            buf.m.offset // offset from querybuf (which buffer we're at)
        );

        if (p == MAP_FAILED) {
            perror("mmap");
            camera_close(camera);
            return NULL;
        }

        camera->mapped_buffers[i].start = p;
        camera->mapped_buffers[i].length = buf.length;

        printf("index %zu, length %zu, offset %u\n",
            i, camera->mapped_buffers[i].length, buf.m.offset);

        // queuing section
        camera->mapped++;
        if (xioctl(camera->fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");
            camera_close(camera);
            return NULL;
        }

        printf("buffer %zu queued: %s\n", i, (buf.flags & V4L2_BUF_FLAG_QUEUED) ? "yes" : "no");
    }

    return camera;
}


void camera_close(camera_t *camera) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (camera == NULL) {
        return;
    }

    if (camera->streaming) {
        xioctl(camera->fd, VIDIOC_STREAMOFF, &type);
    }

    for (size_t i = 0; i < camera->mapped; i++) {
        munmap(camera->mapped_buffers[i].start, camera->mapped_buffers[i].length);
    }

    if (camera->fd >= 0) {
        close(camera->fd);
    }

    free(camera);
}
