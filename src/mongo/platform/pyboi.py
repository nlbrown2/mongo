#!/bin/python

import os
import sys

if (len(sys.argv) < 3):
    print(sys.argv)
    print('Usage: pass in the name of the pipe + JSON args')
    exit(-1)

rd = int(sys.argv[1])
wr = int(sys.argv[2])

# handshake
os.write(wr, '>')

# TODO: reading
