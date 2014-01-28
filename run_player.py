import struct
import subprocess
import time


if __name__ == '__main__':
    ffmpeg = subprocess.Popen(['./clouddisplayplayer', '10.0.33.37', '8000'], stdin=subprocess.PIPE)

    ffmpeg.stdin.write(struct.pack('4siiii', 'POS\n', 100, 100, 100, 100))
    ffmpeg.stdin.flush()

    time.sleep(20)

    ffmpeg.stdin.write(struct.pack('4siiii', 'POS\n', 200, 200, 200, 200))
    ffmpeg.stdin.flush()

    time.sleep(60)
