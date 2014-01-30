#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>


#define kBufferSize 8192

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} PositionCommand;

typedef struct {
    int32_t x;
    int32_t y;
    uint8_t flags;
} MouseCommand;


static FILE* popen_child(const char *argv[], pid_t* child_pid) {
    int pfd[2];
    pid_t pid = 0;
    FILE* f;

    if (pipe(pfd) < 0) return NULL;

    if ((!(f=fdopen(pfd[1], "w"))) ||
        ((pid=fork()) < 0)) {
        close(pfd[0]);
        close(pfd[1]);
        return NULL;
    }

    if (!pid) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(pfd[0], STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
        close(pfd[0]);
        close(pfd[1]);
        execvp(argv[0], (char*const*)argv);
        _exit(127);
    } else {
        close(pfd[0]);
    }

    *child_pid = pid;
    return f;
}

static FILE* respawn_ffplay(const char* ip, const char* port,
                            int32_t x, int32_t y, int32_t width, int32_t height) {
    static pid_t child_pid = 0;
    static FILE* child_fs = NULL;

    if (child_fs) {
        pclose(child_fs);
    }

    if (child_pid) {
        int status = 0;
        kill(child_pid, SIGKILL);
        waitpid(child_pid, &status, 0);
    }

    char command[1024];
    memset(command, 0, sizeof(command));
    snprintf(command, sizeof(command), "%i,%i", x, y);
    setenv("SDL_VIDEO_WINDOW_POS", command, 1);

    char width_str[32] = {0};
    snprintf(width_str, sizeof(width_str), "%i", width);

    char height_str[32] = {0};
    snprintf(height_str, sizeof(height_str), "%i", height);

    char destination_str[256] = {0};
    snprintf(destination_str, sizeof(destination_str), "udp://%s:%s", ip, port);

    const char *command_argv[] = {"ffplay", "-an", "-x", width_str, "-y", height_str, destination_str, 0};

    child_fs = popen_child(command_argv, &child_pid);
    return child_fs;
}

int main(int argc, char const *argv[]) {
    if (argc < 3) {
        printf("%s SRC_IP SRC_PORT\n", argv[0]);
        exit(1);
    }

    FILE* ffplay = NULL;
    PositionCommand current_position;
    memset(&current_position, 0, sizeof(current_position));

    while (1) {
        char command[4] = {0};
        if (fread(&command, sizeof(command), 1, stdin) == 1) {
            if (strncmp(command, "POS\n", 4) == 0) {
                PositionCommand position;
                if (fread(&position, sizeof(position), 1, stdin) != 1) {
                    fprintf(stderr, "invalid params to POS command\n");
                    exit(1);
                }
                if (memcmp(&current_position, &position, sizeof(position)) != 0) {
                    ffplay = respawn_ffplay(argv[1], argv[2],
                        position.x, position.y, position.width, position.height);
                    if (!ffplay) {
                        perror("unable to spawn ffmpeg");
                        exit(1);
                    }
                }
            } else if (strncmp(command, "PTR\n", 4) == 0) {
                PositionCommand mouse;
                if (fread(&mouse, sizeof(mouse), 1, stdin) != 1) {
                    fprintf(stderr, "invalid params to PTR command\n");
                    exit(1);
                }
            } else {
                fprintf(stderr, "invalid command: %s\n", command);
                exit(1);
            }
        } else {
            perror("unable to read header");
            exit(1);
        }
    }
}
