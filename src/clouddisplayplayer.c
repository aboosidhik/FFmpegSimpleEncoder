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
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>

#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_syswm.h>

#include <stdio.h>

#define MAX_FDS_OPEN 512

#define CLOUDDISPLAY_RESIZE_EVENT  (SDL_USEREVENT + 2)

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 288000


#pragma pack(push)
#pragma pack(1)

typedef struct {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
} PositionData;

typedef struct {
  int32_t x;
  int32_t y;
  uint8_t flags;
} MouseData;

#pragma pack(pop)


typedef struct PacketQueue {
  AVPacketList *start, *end;
  int count;
  int size; // sum of sizes for all packets.
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;

static PacketQueue audioQueue;

static AVFormatContext *formatCtx = NULL;

static AVCodecContext *vCodecCtx = NULL;
static AVCodec *vCodec = NULL;
static int videoStream = -1;

static SwrContext *swrCtx = NULL;
static AVCodecContext *aCodecCtx = NULL;
static AVCodec *aCodec = NULL;
static int audioStream = -1;

static SDL_mutex *positionMutex = NULL;
static PositionData currentPosition;

static SDL_mutex *mouseMutex = NULL;
static MouseData currentMouse;


static void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

static void packet_queue_put(PacketQueue *q, AVPacket *pkt) {
  // Duplicate packet if needed.
  if (av_dup_packet(pkt) < 0) {
    fprintf(stderr, "could not set duplicate packet\n");
    exit(1);
  }

  AVPacketList *node = av_malloc(sizeof(AVPacketList));
  node->pkt = *pkt;
  node->next = NULL;

  SDL_LockMutex(q->mutex);

  if (!q->end) {
    q->start = node;
  } else {
    q->end->next = node;
  }

  q->end = node;
  q->count++;
  q->size += pkt->size;
  SDL_CondSignal(q->cond);

  SDL_UnlockMutex(q->mutex);
}

static void packet_queue_get(PacketQueue *q, AVPacket *pkt) {
  SDL_LockMutex(q->mutex);

  while (1) {
    AVPacketList *node = q->start;
    if (node) {
      q->start = node->next;
      if (!q->start) q->end = NULL;

      q->count--;
      q->size -= node->pkt.size;

      *pkt = node->pkt;
      av_free(node);
      break;
    } else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }

  SDL_UnlockMutex(q->mutex);
}


static void sigterm_handler(int sig) __attribute__ ((noreturn));
static void sigterm_handler(int sig) {
  (void)sig; // Supress unused warning.
  exit(123);
}


static int command_thread(void *data) {
  (void)data; // Supress unused warning.

  while (1) {
      char command[4] = {0};
      if (fread(&command, sizeof(command), 1, stdin) == 1) {
          if (strncmp(command, "POS\n", 4) == 0) {
              PositionData position;
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
              MouseData mouse;
              if (fread(&mouse, sizeof(mouse), 1, stdin) != 1) {
                fprintf(stderr, "invalid params to PTR command\n");
                exit(1);
              }
              SDL_mutexP(mouseMutex);
              currentMouse = mouse;
              SDL_mutexV(mouseMutex);
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


static void decodeAndDisplayStream() {
  int frameFinished;

  AVPacket packet;
  AVFrame *frame = NULL;
  struct SwsContext *swsCtx = NULL;

  SDL_Overlay *overlay = NULL;
  SDL_Surface *screen = NULL;
  SDL_Surface *cursor_image = NULL;
  SDL_Rect rect;
  SDL_Event event;
  PositionData position;
  char buffer[1024];

  // Grab the position.
  SDL_mutexP(positionMutex);
  position = currentPosition;
  SDL_mutexV(positionMutex);

  cursor_image = SDL_LoadBMP("cursor.bmp");
  if (!cursor_image) {
    fprintf(stderr, "could not load cursor image: %s\n", SDL_GetError());
  }

  memset(buffer, 0, sizeof(buffer));
  snprintf(buffer, sizeof(buffer), "%i,%i", position.x, position.y);
  setenv("SDL_VIDEO_WINDOW_POS", buffer, 1);

  // Make a screen to put our video
  screen = SDL_SetVideoMode(position.width, position.height, 0,
    SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL | SDL_NOFRAME);

#ifdef __linux__
  SDL_SysWMinfo SysInfo;
  SDL_VERSION(&SysInfo.version);
  if (SDL_GetWMInfo(&SysInfo) <= 0) {
    fprintf(stderr, "unable to get window info: %s\n", SDL_GetError());
    exit(1);
  }

  SysInfo.info.x11.lock_func();
  XRaiseWindow(SysInfo.info.x11.display, SysInfo.info.x11.wmwindow);
  SysInfo.info.x11.unlock_func();
#endif

  if (!screen) {
    fprintf(stderr, "could not set video mode: %s\n", SDL_GetError());
    exit(1);
  }

  // Allocate a place to put our YUV image on that screen
  overlay = SDL_CreateYUVOverlay(position.width, position.height, SDL_YV12_OVERLAY, screen);

  // Allocate video frame
  frame = avcodec_alloc_frame();

  swsCtx = sws_getContext(
    vCodecCtx->width,
    vCodecCtx->height,
    vCodecCtx->pix_fmt,
    position.width,
    position.height,
    PIX_FMT_YUV420P,
    SWS_BILINEAR,
    NULL,
    NULL,
    NULL
  );

  while (av_read_frame(formatCtx, &packet) >= 0) {
    // Is this a packet from the video stream?
    if (packet.stream_index == videoStream) {
      // Decode video frame
      avcodec_decode_video2(vCodecCtx, frame, &frameFinished, &packet);

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
          swsCtx,
          (uint8_t const * const *)frame->data,
          frame->linesize,
          0,
          vCodecCtx->height,
          pict.data,
          pict.linesize
        );

        SDL_UnlockYUVOverlay(overlay);

        rect.x = 0;
        rect.y = 0;
        rect.w = (uint16_t)position.width;
        rect.h = (uint16_t)position.height;
        SDL_DisplayYUVOverlay(overlay, &rect);
      }
      // Free the packet that was allocated by av_read_frame
      av_free_packet(&packet);
    } else if (packet.stream_index == audioStream) {
      packet_queue_put(&audioQueue, &packet);
    } else {
      av_free_packet(&packet);
    }

    // Only draw mouse if the image was loaded correctly.
    if (cursor_image) {
      MouseData mouse;
      SDL_mutexP(mouseMutex);
      mouse = currentMouse;
      SDL_mutexV(mouseMutex);

      if (mouse.flags & 0x01) {
        SDL_Rect pos;
        pos.x = (int16_t)mouse.x;
        pos.y = (int16_t)mouse.y;
        if (SDL_BlitSurface(cursor_image, NULL, screen, &pos) < 0) {
          fprintf(stderr, "BlitSurface error: %s\n", SDL_GetError());
        }
        SDL_UpdateRect(screen, pos.x, pos.y, cursor_image->w, cursor_image->h);
      }
    } else {
      fprintf(stderr, "cursor image not loaded, skipping mouse positioning\n");
    }

    // Drain event pool.
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          SDL_Quit();
          exit(0);
        case CLOUDDISPLAY_RESIZE_EVENT:
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
  sws_freeContext(swsCtx);

  // Free the YUV frame
  av_free(frame);
}


static int audio_decode_frame(uint8_t *audio_buf) {

  static AVPacket pkt;
  static uint8_t *audio_pkt_data = NULL;
  static int audio_pkt_size = 0;
  static AVFrame frame;

  int len1 = 0;
  int data_size = 0;

  while (1) {
    while (audio_pkt_size > 0) {
      int got_frame = 0;
      len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
      if (len1 < 0) {
        /* if error, skip frame */
        audio_pkt_size = 0;
        break;
      }
      audio_pkt_data += len1;
      audio_pkt_size -= len1;
      if (got_frame) {
        data_size = av_samples_get_buffer_size(NULL,
          aCodecCtx->channels, frame.nb_samples, AV_SAMPLE_FMT_S16, 1);
        swr_convert(swrCtx, &audio_buf, frame.nb_samples,
          (const uint8_t**)frame.extended_data, frame.nb_samples);
      }
      if (data_size <= 0) {
        /* No data yet, get more frames */
        continue;
      }
      /* We have data, return it and come back for more later */
      return data_size;
    }

    if (pkt.data) {
      av_free_packet(&pkt);
    }

    packet_queue_get(&audioQueue, &pkt);
    audio_pkt_data = pkt.data;
    audio_pkt_size = pkt.size;
  }
}

static void audio_pull_from_queue(void *userdata, uint8_t *stream, int len) {
  static uint8_t audio_buf[MAX_AUDIO_FRAME_SIZE];
  static int audio_buf_size = 0;
  static int audio_buf_index = 0;

  (void)userdata; // Supress unused warning.

  int len1 = 0;
  int audio_size = 0;

  while (len > 0) {
    if (audio_buf_index >= audio_buf_size) {
      // We have already sent all our data. Get more.
      audio_size = audio_decode_frame(audio_buf);
      if (audio_size < 0) {
        /* If error, output silence */
        audio_buf_size = 1024; // arbitrary?
        memset(audio_buf, 0, audio_buf_size);
      } else {
        audio_buf_size = audio_size;
      }
      audio_buf_index = 0;
    }
    len1 = audio_buf_size - audio_buf_index;
    if (len1 > len) len1 = len;
    memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
    len -= len1;
    stream += len1;
    audio_buf_index += len1;
  }
}


int main(int argc, char *argv[]) {
  char input_str[256] = {0};
  AVDictionary *videoOptionsDict = NULL;
  AVDictionary *audioOptionsDict = NULL;

  if (argc < 3) {
    fprintf(stderr, "Usage: clouddisplayplayer SRC_IP SRC_PORT\n");
    exit(1);
  }

  // Close all file descriptors except the standard ones
  for (int i = STDERR_FILENO + 1; i < MAX_FDS_OPEN; ++i) {
    close(i);
  }

  // Redirect stdout and stderr to /dev/null.
  {
    int fd = open("/dev/null", O_RDWR);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
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

  // Global mouse initialization
  memset(&currentMouse, 0, sizeof(currentMouse));
  mouseMutex = SDL_CreateMutex();

  // Start thread that will read commands from stdin.
  SDL_CreateThread(command_thread, NULL);

  // Open video stream. Might block.
  snprintf(input_str, sizeof(input_str), "udp://%s:%s", argv[1], argv[2]);
  if (avformat_open_input(&formatCtx, input_str, NULL, NULL) != 0) {
    fprintf(stderr, "Could not open video stream\n");
    return -1;
  }

  // Retrieve stream information. Might block.
  if (avformat_find_stream_info(formatCtx, NULL) < 0) {
    fprintf(stderr, "Unable to find stream information\n.");
    return -1; // Couldn't find stream information
  }

  // Find the first video stream
  for (size_t i = 0; i < formatCtx->nb_streams; i++) {
    if (videoStream < 0 && formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStream = i;
    }
    if (audioStream < 0 && formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
      audioStream = i;
    }
  }

  if (videoStream == -1) {
    fprintf(stderr, "Unable to find a video in the stream\n");
    return -1; // Didn't find a video stream
  }

  // Get a pointer to the codec context for the video stream
  vCodecCtx = formatCtx->streams[videoStream]->codec;

  // Find the decoder for the video stream
  vCodec = avcodec_find_decoder(vCodecCtx->codec_id);
  if (vCodec == NULL) {
    fprintf(stderr, "unsupported video codec!\n");
    return -1; // Codec not found
  }

  // Open video codec
  if (avcodec_open2(vCodecCtx, vCodec, &videoOptionsDict) < 0) {
    fprintf(stderr, "unable to open video codec\n");
    return -1; // Could not open codec
  }

  if (audioStream > 0) {
    aCodecCtx = formatCtx->streams[audioStream]->codec;

    aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
    if(aCodec == NULL) {
      fprintf(stderr, "unsupported audio codec\n");
      return -1;
    }

    // Open audio codec
    if (avcodec_open2(aCodecCtx, aCodec, &audioOptionsDict) < 0) {
      fprintf(stderr, "unable to open audio codec\n");
      return -1; // Could not open codec
    }

    swrCtx = swr_alloc();

    av_opt_set_int(swrCtx, "in_channel_layout", aCodecCtx->channel_layout, 0);
    av_opt_set_int(swrCtx, "in_sample_fmt", aCodecCtx->sample_fmt, 0);
    av_opt_set_int(swrCtx, "in_sample_rate", aCodecCtx->sample_rate, 0);

    av_opt_set_int(swrCtx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int(swrCtx, "out_sample_rate", aCodecCtx->sample_rate, 0);

    if (swr_init(swrCtx) < 0) {
      fprintf(stderr, "Unsupported resampler!\n");
      exit(1);
    }

    SDL_AudioSpec wantedSpec, actualSpec;
    // Set audio settings from codec info
    wantedSpec.freq = aCodecCtx->sample_rate;
    wantedSpec.format = AUDIO_S16SYS;
    wantedSpec.channels = aCodecCtx->channels;
    wantedSpec.silence = 0;
    wantedSpec.samples = SDL_AUDIO_BUFFER_SIZE;
    wantedSpec.callback = audio_pull_from_queue;
    wantedSpec.userdata = aCodecCtx;

    if (SDL_OpenAudio(&wantedSpec, &actualSpec) < 0) {
      fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
      return -1;
    }

    packet_queue_init(&audioQueue);
    SDL_PauseAudio(0);
  }

  // Wait for resize events to restart the player.
  while (1) {
    SDL_Event event;
    if (SDL_WaitEvent(&event) == 1) {
      if (event.type == CLOUDDISPLAY_RESIZE_EVENT) {
        decodeAndDisplayStream();
      }
    }
  }
}
