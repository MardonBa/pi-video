#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "camera.h"
#include <stdbool.h>
#include <inttypes.h>

int main(void) {
    camera_t *camera = camera_open("/dev/video0", 640, 480);

    if (camera == NULL) {
        return 1;
    }

    if (camera_on(camera) < 0) {
        camera_close(camera);
        return 1;
    }

    // enqueue/dequeue loop
    frame_t frame = {0};
    frame.pixel_format = PIX_YUYV; // currently known
    frame.bytesize = camera->fmt.size;
    frame.data = malloc(camera->fmt.size);
    if (frame.data == NULL) {
        // malloc failed
        camera_close(camera);
        return 1;
    }
    frame.capacity = camera->fmt.size;
    frame.height = camera->fmt.height;
    frame.width = camera->fmt.width;
    frame.stride = camera->fmt.stride;

    for (int i = 0; i < 200; i++) { // looping for 200 frames, placeholder for future
        if (camera_get_frame(camera, &frame) == -1) {
            free(frame.data);
            camera_close(camera);
            return 1;
        }

        if (i < 10) {
            continue; // skip first 10 frames
        }

        if (i == 100) {
            FILE *fp = fopen("frame_100.yuv", "wb");

            if (fp == NULL) {
                perror("failed to open file for writing");
            } else {
                size_t n = fwrite(frame.data, 1, frame.bytesize, fp);
                if (n != frame.bytesize) {
                    fprintf(stderr, "short write: %zu of %zu bytes\n", n, frame.bytesize);
                }
                fclose(fp); // only call if fopen() succeeds
            }
        }
    }

    // teardown and completion
    free(frame.data);
    camera_close(camera);
    return 0;
}
