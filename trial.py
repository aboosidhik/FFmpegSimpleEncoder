import glob
import struct
import subprocess
import sys
import time
from PIL import Image


if __name__ == '__main__':
    ffmpeg = subprocess.Popen(['./clouddisplayencoder', '10.0.33.210', '8000', 'RGB888'], stdin=subprocess.PIPE)

    for infile in glob.glob('images/*.jpeg'):
        im = Image.open(infile)
        ffmpeg.stdin.write(struct.pack('4sii', 'FRM\n', *im.size))
        ffmpeg.stdin.write(im.tostring('raw', 'RGB'))
        time.sleep(0.5)
