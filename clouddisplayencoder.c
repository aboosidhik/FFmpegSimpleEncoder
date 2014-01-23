#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>


#define kBufferSize 4098

typedef struct {
    char tag[4];
    int32_t width;
    int32_t height;
} FrameHeader;


static size_t pix_fmt_to_bits_per_pixel(const char* pix_fmt) {
    if (strcmp(pix_fmt, "RGB888") == 0) {
        return 3;
    } else if (strcmp(pix_fmt, "BGR888") == 0) {
        return 3;
    } else if (strcmp(pix_fmt, "ABGR8888") == 0) {
        return 4;
    } else if (strcmp(pix_fmt, "ARGB8888") == 0) {
        return 4;
    } else {
        printf("Error! Invalid PIX_FMT: %s\n", pix_fmt);
        exit(1);
    }
}


static FILE* spawn_ffmpeg(const char* ip, const char* port, const char* pix_fmt,
                          int width, int height) {
    if (strcmp(pix_fmt, "RGB888") == 0) {
        pix_fmt = "rgb24";
    } else if (strcmp(pix_fmt, "BGR888") == 0) {
        pix_fmt = "bgr24";
    } else if (strcmp(pix_fmt, "ABGR8888") == 0) {
        pix_fmt = "abgr";
    } else if (strcmp(pix_fmt, "ARGB8888") == 0) {
        pix_fmt = "argb";
    } else {
        printf("Error! Invalid PIX_FMT: %s\n", pix_fmt);
        exit(1);
    }

    char command[4096] = {'\0'};
    snprintf(command, 4096,
        "ffmpeg -f rawvideo -pix_fmt %s -s %ix%i -r 2 -i - "
        "-threads 0 -vcodec libx264 -g 1 -preset ultrafast -tune zerolatency "
        "-crf 20 -f mpegts udp://%s:%s",
        pix_fmt, width, height, ip, port);
    printf("command: %s\n", command);
    return popen(command, "w");
}


int main(int argc, char const *argv[]) {
    if (argc < 4) {
        printf("%s DEST_IP DEST_PORT PIX_FMT\n", argv[0]);
        exit(1);
    }

    FILE* ffmpeg = NULL;
    size_t bits_per_pixel = pix_fmt_to_bits_per_pixel(argv[3]);
    uint8_t buffer[kBufferSize] = {0};

    while (1) {
        FrameHeader header;
        memset(&header, 0, sizeof(header));

        if (fread(&header, sizeof(header), 1, stdin) == 1) {
            if (strncmp(header.tag, "FRM\n", 4) != 0) {
                fprintf(stderr, "Invalid header: %s\n", header.tag);
                exit(1);
            }

            if (!ffmpeg) {
                ffmpeg = spawn_ffmpeg(argv[1], argv[2], argv[3], header.width, header.height);
                if (!ffmpeg) {
                    perror("unable to spawn ffmpeg");
                    exit(1);
                }
            }

            size_t remaining = (size_t)(header.width * header.height) * bits_per_pixel;
            while (remaining > 0) {
                if (remaining > kBufferSize) {
                    fread(buffer, kBufferSize, 1, stdin);
                    fwrite(buffer, kBufferSize, 1, ffmpeg);
                    remaining -= kBufferSize;
                } else {
                    fread(buffer, remaining, 1, stdin);
                    fwrite(buffer, remaining, 1, ffmpeg);
                    remaining = 0;
                }
            }
        } else {
            perror("unable to read header");
            exit(1);
        }
    }
}
