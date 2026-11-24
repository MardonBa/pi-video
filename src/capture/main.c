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
#include "camera.h"

static int xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);

    return r;
}

int main(void) {
    camera_t *camera = camera_open("/dev/video0", 640, 480);

    if (camera == NULL) {
        return 1;
    }

    // placeholder since refactor isn't completely done
    struct buf_info *dequeued_buffers = camera->mapped_buffers;

    // start streaming buffers

    int buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(camera->fd, VIDIOC_STREAMON, &buffer_type) == -1) {
        perror("VIDIOC_STREAMON");
        camera_close(camera);
        return 1;
    }

    printf("streaming on\n");
    camera->streaming = true;

    // enqueue/dequeue loop

    for (int i = 0; i < 200; i++) { // looping for 200 frames, placeholder for future

        struct pollfd pfd = {
            .fd = camera->fd,
            .events = POLLIN
        };
        int r = poll(&pfd, 1, 2000); // Returns a frame as soon as one is receieved, up to 2 seconds

        if (r > 0 && pfd.revents & POLLIN) {
            // pass through, all is well
        } else if (r == 0) {
            printf("Timed out with no frame\n");

            camera_close(camera);
            return 1;
        } else {
            perror("poll");
            camera_close(camera);
            return 1;
        }

        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(camera->fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("VIDIOC_DQBUF");
            camera_close(camera);
            return 1;
        }

        if (buf.flags & V4L2_BUF_FLAG_ERROR) {
            if (xioctl(camera->fd, VIDIOC_QBUF, &buf) == -1) {
                perror("VIDIOC_QBUF");
                camera_close(camera);
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

        if (xioctl(camera->fd, VIDIOC_QBUF, &buf) == -1) { // we always queue back the buffers
            perror("VIDIOC_QBUF");
            camera_close(camera);
            return 1;
        }
    }

    // teardown and completion
    camera_close(camera);
    return 0;
}
