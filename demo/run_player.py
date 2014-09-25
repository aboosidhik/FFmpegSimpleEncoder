import argparse
import struct
import subprocess
import time


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Sample spawner for clouddisplayplayer', conflict_handler='resolve')
    parser.add_argument('--player', default='./clouddisplayplayer', metavar='PATH')
    parser.add_argument('-x', default=100, type=int, metavar='POS_X')
    parser.add_argument('-y', default=100, type=int, metavar='POS_Y')
    parser.add_argument('-w', default=320, type=int, metavar='WIDTH')
    parser.add_argument('-h', default=180, type=int, metavar='HEIGHT')
    parser.add_argument('-p', '--port', default='8000')
    parser.add_argument('host')

    args = parser.parse_args()

    player = subprocess.Popen([args.player, args.host, args.port], stdin=subprocess.PIPE)

    player.stdin.write(struct.pack('4siiii', 'POS\n', args.x, args.y, args.w, args.h))
    player.stdin.flush()

    while True:
        time.sleep(600)
