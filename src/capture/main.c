#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>


static int xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);

    return r;
}

int main(void) {
    const char *dev_name = "/dev/video0";
    int fd = open(dev_name, O_RDWR);
    if (fd < 0) { // failed to open the device, fail quickly
        fprintf(stderr, "Failed to open camera (errno=%d): %s\n", errno, strerror(errno));
        close(fd);
        return 1;
    }

    // Setting format & framerate

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1)  {
        perror("VIDIOC_S_FMT");
        close(fd);
        return 1;
    }

    printf("type:         %u (expected %u)\n", fmt.type, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    printf("width:        %u (expected 640)\n", fmt.fmt.pix.width);
    printf("height:       %u (expected 480)\n", fmt.fmt.pix.height);
    printf("pixelformat:  %c%c%c%c (expected YUYV)\n",
           fmt.fmt.pix.pixelformat & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
    printf("field:        %u (expected %u)\n", fmt.fmt.pix.field, V4L2_FIELD_NONE);
    printf("bytesperline: %u (expected 1280)\n", fmt.fmt.pix.bytesperline);
    printf("sizeimage:    %u (expected 614400)\n", fmt.fmt.pix.sizeimage);

    // Requesting buffers, checking how many are received
    struct v4l2_requestbuffers buffs = {0};
    buffs.count = 4;
    buffs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffs.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd, VIDIOC_REQBUFS, buffs) == -1) {
        fprintf(stderr, "Failed to open camera (errno=%d): %s\n", errno, strerror(errno));
        close(fd);
        return 1;
    }

    printf("count:  %u (requested 4)\n", buffs.count);
    printf("type:   %u (expected %u)\n", buffs.type, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    printf("memory: %u (expected %u)\n", buffs.memory, V4L2_MEMORY_MMAP);

    close(fd);
    return 0;
}
