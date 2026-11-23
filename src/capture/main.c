#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <poll.h>
#include <stdint.h>

struct buf_info {
    void *start;
    size_t length;
};

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

    if (xioctl(fd, VIDIOC_REQBUFS, &buffs) == -1) {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return 1;
    }

    if (buffs.count < 2 || buffs.count > 8) {
        fprintf(stderr, "unexpected buffer count: %u\n", buffs.count);
        close(fd);
        return 1;
    }

    printf("count:  %u (requested 4)\n", buffs.count);
    printf("type:   %u (expected %u)\n", buffs.type, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    printf("memory: %u (expected %u)\n", buffs.memory, V4L2_MEMORY_MMAP);

    // map buffers to user address space

    struct buf_info dequeued_buffers[8]; // up to 8 buffers, realistically we'll have 4 but space for more in case

    for (size_t i = 0; i < buffs.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = buffs.type;
        buf.memory = buffs.memory;
        buf.index = i;

        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("VIDIOC_QUERYBUF");

            // clears all previous buffers since we ran into an error
            // munmap removes memory mappings created by mmap
            for (size_t j = 0; j < i; j++) {
                munmap(dequeued_buffers[j].start, dequeued_buffers[j].length);
            }

            close(fd);
            return 1;
        }

        void *p = mmap(
            NULL, // address, kernel will choose this
            buf.length, // size the driver reports for the buffer (from QUERY_BUF)
            PROT_READ | PROT_WRITE, // you can read and write
            MAP_SHARED, // share memory with driver, so you can see what the camera writes
            fd, // what devices the buffers are from
            buf.m.offset // offset from querybuf (which buffer we're at)
        );

        if (p == MAP_FAILED) {
            perror("mmap");
            for (size_t j = 0; j < i; j++) {
                munmap(dequeued_buffers[j].start, dequeued_buffers[j].length);
            }
            close(fd);
            return 1;
        }

        dequeued_buffers[i].start = p;
        dequeued_buffers[i].length = buf.length;

        printf("index %zu, length %zu, offset %u\n", i, dequeued_buffers[i].length, buf.m.offset);
    }

    // queue buffers for the first frames

    for (size_t i = 0; i < buffs.count; i++) {
        struct v4l2_buffer buf = {0};

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("VIDIOC_QBUF");

            for (size_t j = 0; j < buffs.count; j++) {
                munmap(dequeued_buffers[j].start, dequeued_buffers[j].length);
            }

            close(fd);
            return 1;
        }

        printf("buffer %zu queued: %s\n", i, (buf.flags & V4L2_BUF_FLAG_QUEUED) ? "yes" : "no");
    }

    // start streaming buffers

    int buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(fd, VIDIOC_STREAMON, &buffer_type) == -1) {
        perror("VIDIOC_STREAMON");

        for (size_t i = 0; i < buffs.count; i++) {
            munmap(dequeued_buffers[i].start, dequeued_buffers[i].length);
        }

        close(fd);
        return 1;
    }

    printf("streaming on\n");

    // enqueue/dequeue loop

    for (int i = 0; i < 200; i++) { // looping for 200 frames, placeholder for future

        struct pollfd pfd = {
            .fd = fd,
            .events = POLLIN
        };
        int r = poll(&pfd, 1, 2000); // Returns a frame as soon as one is receieved, up to 2 seconds

        if (r > 0 && pfd.revents & POLLIN) {
            // pass through, all is well
        } else if (r == 0) {
            printf("Timed out with no frame\n");

            for (size_t j = 0; j < buffs.count; j++) {
                munmap(dequeued_buffers[j].start, dequeued_buffers[j].length);
            }

            close(fd);
            return 1;
        } else {
            perror("poll");

            for (size_t j = 0; j < buffs.count; j++) {
                munmap(dequeued_buffers[j].start, dequeued_buffers[j].length);
            }

            close(fd);
            return 1;
        }

        struct v4l2_buffer buf = {0};
        buf.type = buffs.type;
        buf.memory = buffs.memory;

        if (xioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("VIDIOC_DQBUF");

            for (size_t j = 0; j < buffs.count; j++) {
                munmap(dequeued_buffers[j].start, dequeued_buffers[j].length);
            }

            close(fd);
            return 1;
        }

        if (buf.flags & V4L2_BUF_FLAG_ERROR) {
            if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
                perror("VIDIOC_QBUF");

                // need to stop streaming, don't care about failure since we're terminating anyways
                xioctl(fd, VIDIOC_STREAMOFF, &buffer_type);
                for (size_t j = 0; j < buffs.count; j++) {
                    munmap(dequeued_buffers[j].start, dequeued_buffers[j].length);
                }

                close(fd);
                return 1;
            }
            continue;
        }

        // we've dequeued the buffer at this point, now we need to use the frame
        // for now, i'll compute average brightness and write the bytes to a file

        if (i >= 10) { // we only want to operate on the frames after the first 10
            uint64_t sum = 0;
            const uint8_t *p = (uint8_t *)dequeued_buffers[buf.index].start;
            for (uint32_t j = 0; j < buf.bytesused; j += 2) {
                sum += *(p + j); // equivalent to p[j], but I like this more because I can flex my pointer arithmetic knowledge
            }

            double avg = (double) sum / (buf.bytesused / 2);

            printf("index %d, sequence %u, bytes used %u, avg brightness %.1f\n", i, buf.sequence, buf.bytesused, avg);

            if (i == 100) { // we'll save the 100th frame to a file as a representative
                FILE *fp = fopen("frame_100.yuv", "wb");

                if (fp == NULL) {
                    perror("failed to open file for writing");
                } else {
                    size_t n = fwrite(p, 1, buf.bytesused, fp);
                    if (n != buf.bytesused) {
                        fprintf(stderr, "short write: %zu of %u bytes\n", n, buf.bytesused);
                    }

                    fclose(fp);
                }
            }
        }

        if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) { // we always queue back the buffers
            perror("VIDIOC_QBUF");

            // need to stop streaming, don't care about failure since we're terminating anyways
            xioctl(fd, VIDIOC_STREAMOFF, &buffer_type);
            for (size_t j = 0; j < buffs.count; j++) {
                munmap(dequeued_buffers[j].start, dequeued_buffers[j].length);
            }

            close(fd);
            return 1;
        }
    }

    // teardown and completion

    if (xioctl(fd, VIDIOC_STREAMOFF, &buffer_type) == -1) {
        perror("VIDIOC_STREAMOFF");
    }

    for (size_t i = 0; i < buffs.count; i++) {
        munmap(dequeued_buffers[i].start, dequeued_buffers[i].length);
    }

    close(fd);
    return 0;
}
