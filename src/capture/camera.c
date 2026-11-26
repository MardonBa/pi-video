#include <asm-generic/errno-base.h>
#include <bits/types.h>
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


void camera_close(camera_t *camera) {
    if (camera == NULL) {
        return;
    }

    if (camera->streaming) {
        camera_off(camera);
    }

    for (size_t i = 0; i < camera->mapped; i++) {
        munmap(camera->mapped_buffers[i].start, camera->mapped_buffers[i].length);
    }

    if (camera->fd >= 0) {
        close(camera->fd);
    }

    free(camera);
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
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
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

int camera_on(camera_t *camera) {
    int buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(camera->fd, VIDIOC_STREAMON, &buffer_type) == -1) {
        perror("VIDIOC_STREAMON");
        return -1;
    }

    printf("streaming on\n");
    camera->streaming = true;

    return 0;
}

int camera_off(camera_t *camera) {
    int buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(camera->fd, VIDIOC_STREAMOFF, &buffer_type) == -1) {
        perror("VIDIOC_STREAMOFF");
        return -1;
    }

    printf("streaming off\n");
    camera->streaming = false;

    return 0;
}

int camera_get_frame(camera_t *camera, frame_t *frame) {

    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    for (;;) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        struct pollfd pfd = {
            .fd = camera->fd,
            .events = POLLIN
        };
        int r = poll(&pfd, 1, 2000); // Returns a frame as soon as one is receieved, up to 2 seconds

        if (r == 0) {
            printf("Timed out with no frame\n");
            return -1;
        } else if (r < 0) {
            perror("poll");
            return -1;
        }

        // if we get here, all is well

        if (xioctl(camera->fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("VIDIOC_DQBUF");
            return -1;
        }

        if (buf.flags & V4L2_BUF_FLAG_ERROR) {
            if (xioctl(camera->fd, VIDIOC_QBUF, &buf) == -1) {
                perror("VIDIOC_QBUF");
                return -1;
            }
            continue;
        } else {
            break;
        }
    }

    if (buf.index < camera->mapped && buf.bytesused <= frame->capacity) {
        memcpy(frame->data, camera->mapped_buffers[buf.index].start, buf.bytesused);
        frame->bytesize = buf.bytesused;
        frame->sequence = buf.sequence;
        frame->timestamp = (uint64_t)buf.timestamp.tv_sec * 1000000 + buf.timestamp.tv_usec;
    } else {
        return -1;
    }

    if (xioctl(camera->fd, VIDIOC_QBUF, &buf) == -1) { // we always queue back the buffers
        perror("VIDIOC_QBUF");
        return -1;
    }

    return 0;
}

static const char *ctrl_type_name(__u32 type) {
    switch (type) {
        case V4L2_CTRL_TYPE_INTEGER:      return "integer";
        case V4L2_CTRL_TYPE_BOOLEAN:      return "boolean";
        case V4L2_CTRL_TYPE_MENU:         return "menu";
        case V4L2_CTRL_TYPE_BUTTON:       return "button";
        case V4L2_CTRL_TYPE_INTEGER64:    return "integer64";
        case V4L2_CTRL_TYPE_CTRL_CLASS:   return "class";
        case V4L2_CTRL_TYPE_STRING:       return "string";
        case V4L2_CTRL_TYPE_BITMASK:      return "bitmask";
        case V4L2_CTRL_TYPE_INTEGER_MENU: return "int menu";
        default:                          return "other";
    }
}

static int camera_get_control(camera_t *camera, __u32 id, int *value) {
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;

    if (xioctl(camera->fd, VIDIOC_G_CTRL, &ctrl) == -1) {
        perror("VIDIOC_G_CTRL failed");
        return -1;
    }

    *value = ctrl.value;
    return 0;
}

int camera_list_controls(camera_t *camera) {
    struct v4l2_queryctrl qc = {0};
    qc.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    while (xioctl(camera->fd, VIDIOC_QUERYCTRL, &qc) == 0) {
        // handle control, skip if disabeld (check flag)
        // list menu entries if it's a menu
        __u32 flag = qc.flags;

        if (flag & V4L2_CTRL_FLAG_DISABLED) {
            printf("control disabled, skipping");
        } else { // for now we don't care about the other flags other than that they are set
            printf("0x%08x %-32s %-9s min=%d max=%d step=%d default=%d flags=0x%x",
                       qc.id, (const char *)qc.name, ctrl_type_name(qc.type),
                       qc.minimum, qc.maximum, qc.step, qc.default_value, qc.flags);

            if (qc.flags & V4L2_CTRL_FLAG_GRABBED)              printf(" grabbed");
            if (qc.flags & V4L2_CTRL_FLAG_READ_ONLY)            printf(" read-only");
            if (qc.flags & V4L2_CTRL_FLAG_UPDATE)               printf(" update");
            if (qc.flags & V4L2_CTRL_FLAG_INACTIVE)             printf(" inactive");
            if (qc.flags & V4L2_CTRL_FLAG_SLIDER)               printf(" slider");
            if (qc.flags & V4L2_CTRL_FLAG_WRITE_ONLY)           printf(" write-only");
            if (qc.flags & V4L2_CTRL_FLAG_VOLATILE)             printf(" volatile");
            if (qc.flags & V4L2_CTRL_FLAG_HAS_WHICH_MIN_MAX)    printf(" has min-max");
            printf("\n");

            int value;
            if (camera_get_control(camera, qc.id, &value) == 0) {
                printf("  current value: %d\n", value);
            }

        qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
        }
    }

    if (errno != EINVAL) {
        perror("VIDIOC_QUERYCTRL");
        return -1;
    }

    return 0;
}
