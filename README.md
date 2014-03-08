# CloudDisplayFFmpeg


## Using `clouddisplayencoder`

The encoder process must be spawned with the following parameters:


    ./clouddisplayencoder DEST_IP DEST_PORT WIDTH HEIGHT PIX_FMT [AUD_FMT SAMPLE_RATE]


*WIDTH* and *HEIGHT* are in pixels and must be even numbers, preferably multiples of 16 to enable `asm` optimizations in FFmpeg.

*PIX_FMT* must be one of:
- `ABGR8888` *Alpha Blue Green Red, 8 bits per pixel*
- `ARGB8888` *Alpha Red Green Blue, 8 bits per pixel*
- `BGR888` *Blue Green Red, 8 bits per pixel*
- `BGRA8888` *Blue Green Red Alpha, 8 bits per pixel*
- `RGB888` *Red Green Blue, 8 bits per pixel*
- `RGBA8888` *Red Green Blue Alpha, 8 bits per pixel*

*AUD_FMT* must be one of:
- `PCMF32LE` *PCM 32-bit floating-point little-endian*
- `PCMS16LE` *PCM signed 16-bit little-endian*

*SAMPLE_RATE* is number of samples per second.

Both *AUD_FMT* and *SAMPLE_RATE* can be omitted if audio encoding is not desired.

### Feeding data to `clouddisplayencoder`

Once the encoder is spawned, one must feed data for it via the standard in pipe using one of the commands listed bellow:

---

Bytes    | Format     | Description
-------- | ---------- | ---------------------------------
 0 - 3   | 'FRM\n'    | Command for video frame
 4 - 11  | uint64_t   | Capture timestamp in microseconds
12 -     | uint8_t    | Video data

**IMPORTANT**: Video data size is `BYTES_PER_PIXEL * WIDTH * HEIGHT`.

---

Bytes    | Format     | Description
-------- | ---------- | ---------------------------------
 0 - 3   | 'AUD\n'    | Command for audio frame
 4 - 11  | uint64_t   | Capture timestamp in microseconds
12 - 15  | uint32_t   | Number of samples
16 -     | uint8_t    | Audio data

**IMPORTANT**: Audio data is always stereo, so audio data size is `BYTES_PER_SAMPLE * NUMBER_OF_SAMPLES * 2 CHANNELS`.
Samples from channels are interleaved, so `DATA[i]` is left channel and `DATA[i + 1]` is right channel for `i % 2`.
