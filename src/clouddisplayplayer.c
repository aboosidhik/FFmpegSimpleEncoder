/**
 * Copyright (C) 2014 Panasonic Corporation of North America.
 *
 * Code based on FFplay, Copyright (C) 2003 Fabrice Bellard
 * and a tutorial by Martin Bohme (boehme@inb.uni-luebeck.de),
 * with adaptations by Stephen Dranger (dranger@gmail.com)
 * and updates by Michael Penkov (misha.penkov@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
**/
#include <sys/types.h>
#include <signal.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#include <stdio.h>


#define CLOUDDISPLAY_RESIZE_EVENT  (SDL_USEREVENT + 2)

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} PositionCommand;


static int videoStream;
static AVFormatContext *pFormatCtx = NULL;
static AVCodecContext *pCodecCtx = NULL;
static AVCodec *pCodec = NULL;

SDL_mutex *positionMutex = NULL;
static PositionCommand currentPosition;


static void sigterm_handler(int sig) {
  (void)sig; // Supress unused warning.
  exit(123);
}


int command_thread(void *data) {
  (void)data; // Supress unused warning.

  while (1) {
      char command[4] = {0};
      if (fread(&command, sizeof(command), 1, stdin) == 1) {
          if (strncmp(command, "POS\n", 4) == 0) {
              PositionCommand position;
              if (fread(&position, sizeof(position), 1, stdin) != 1) {
                  fprintf(stderr, "invalid params to POS command\n");
                  exit(1);
              }
              SDL_mutexP(positionMutex);
              if (memcmp(&currentPosition, &position, sizeof(position)) != 0) {
                currentPosition = position;
                SDL_Event event;
                event.type = CLOUDDISPLAY_RESIZE_EVENT;
                SDL_PushEvent(&event);
              }
              SDL_mutexV(positionMutex);
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
  return 0;
}


static void displayVideoRectangle() {
  int frameFinished;

  AVPacket packet;
  AVFrame *pFrame = NULL;
  struct SwsContext *sws_ctx = NULL;

  SDL_Overlay *overlay = NULL;
  SDL_Surface *screen = NULL;
  SDL_Rect rect;
  SDL_Event event;
  PositionCommand position;
  char buffer[1024];

  // Grab the position.
  SDL_mutexP(positionMutex);
  position = currentPosition;
  SDL_mutexV(positionMutex);

  memset(buffer, 0, sizeof(buffer));
  snprintf(buffer, sizeof(buffer), "%i,%i", position.x, position.y);
  setenv("SDL_VIDEO_WINDOW_POS", buffer, 1);

  // Make a screen to put our video
  screen = SDL_SetVideoMode(position.width, position.height, 0,
    SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL | SDL_NOFRAME);

  if (!screen) {
    fprintf(stderr, "SDL: could not set video mode - exiting\n");
    exit(1);
  }

  // Allocate a place to put our YUV image on that screen
  overlay = SDL_CreateYUVOverlay(position.width, position.height, SDL_YV12_OVERLAY, screen);

  // Allocate video frame
  pFrame = avcodec_alloc_frame();

  sws_ctx = sws_getContext(
    pCodecCtx->width,
    pCodecCtx->height,
    pCodecCtx->pix_fmt,
    position.width,
    position.height,
    PIX_FMT_YUV420P,
    SWS_BILINEAR,
    NULL,
    NULL,
    NULL
  );

  while (av_read_frame(pFormatCtx, &packet) >= 0) {
    // Is this a packet from the video stream?
    if (packet.stream_index == videoStream) {
      // Decode video frame
      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

      // Did we get a video frame?
      if (frameFinished) {
        SDL_LockYUVOverlay(overlay);

        AVPicture pict;
        pict.data[0] = overlay->pixels[0];
        pict.data[1] = overlay->pixels[2];
        pict.data[2] = overlay->pixels[1];

        pict.linesize[0] = overlay->pitches[0];
        pict.linesize[1] = overlay->pitches[2];
        pict.linesize[2] = overlay->pitches[1];

        // Convert the image into YUV format that SDL uses
        sws_scale(
          sws_ctx,
          (uint8_t const * const *)pFrame->data,
          pFrame->linesize,
          0,
          pCodecCtx->height,
          pict.data,
          pict.linesize
        );

        SDL_UnlockYUVOverlay(overlay);

        rect.x = 0;
        rect.y = 0;
        rect.w = position.width;
        rect.h = position.height;
        SDL_DisplayYUVOverlay(overlay, &rect);
      }
    }

    // Free the packet that was allocated by av_read_frame
    av_free_packet(&packet);

    // Drain event pool.
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          SDL_Quit();
          exit(0);
          break;
        case CLOUDDISPLAY_RESIZE_EVENT:
          printf("Push the event again!\n");
          // Push the event again so main-loop can call us again.
          SDL_PushEvent(&event);
          goto cleanup;
        default:
          break;
      }
    }
  }

cleanup:
  SDL_FreeYUVOverlay(overlay);

  // Free software scaling context.
  sws_freeContext(sws_ctx);

  // Free the YUV frame
  av_free(pFrame);
}


int main(int argc, char *argv[]) {
  unsigned int i = 0;
  char input_str[256] = {0};
  AVDictionary *optionsDict = NULL;

  if (argc < 3) {
    fprintf(stderr, "Usage: clouddisplayplayer SRC_IP SRC_PORT\n");
    exit(1);
  }

  // Register a few signals to avoid blocking forever.
  signal(SIGINT, sigterm_handler);
  signal(SIGTERM, sigterm_handler);

  // Register all formats and codecs
  av_register_all();
  avformat_network_init();

  // Initialize SDL.
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  // Global position initialization
  memset(&currentPosition, 0, sizeof(currentPosition));
  positionMutex = SDL_CreateMutex();

  // Start thread that will read commands from stdin.
  SDL_CreateThread(command_thread, NULL);

  // Open video stream. Might block.
  snprintf(input_str, sizeof(input_str), "udp://%s:%s", argv[1], argv[2]);
  if (avformat_open_input(&pFormatCtx, input_str, NULL, NULL) != 0) {
    fprintf(stderr, "Could not open video stream\n");
    return -1;
  }

  // Retrieve stream information. Might block.
  if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
    fprintf(stderr, "Unable to find stream information\n.");
    return -1; // Couldn't find stream information
  }

  // Find the first video stream
  videoStream = -1;
  for (i = 0; i < pFormatCtx->nb_streams; i++) {
    if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStream = i;
      break;
    }
  }
  if (videoStream == -1) {
    fprintf(stderr, "Unable to find a video in the stream\n");
    return -1; // Didn't find a video stream
  }

  // Get a pointer to the codec context for the video stream
  pCodecCtx = pFormatCtx->streams[videoStream]->codec;

  // Find the decoder for the video stream
  pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
  if (pCodec == NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1; // Codec not found
  }

  // Open codec
  if (avcodec_open2(pCodecCtx, pCodec, &optionsDict) < 0) {
    fprintf(stderr, "Unable to open code for decoding\n");
    return -1; // Could not open codec
  }

  // Wait for resize events to restart the player.
  while (1) {
    SDL_Event event;
    if (SDL_WaitEvent(&event) == 1) {
      if (event.type == CLOUDDISPLAY_RESIZE_EVENT) {
        printf("Running rectangle\n");
        displayVideoRectangle();
      }
    }
  }

  // Close the codec
  avcodec_close(pCodecCtx);

  // Close the video stream
  avformat_close_input(&pFormatCtx);

  return 0;
}
