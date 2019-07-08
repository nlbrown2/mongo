#!/usr/bin/python3
""" This module can load a JSON configuration for testing eBPF probes and generate eBPF programs, attach them to the specified probes, and verify their output"""

import os
import sys
from tester import tester



def main():
    #pylint: disable=missing-docstring
    # parse command line args
    if len(sys.argv) < 4:
        print("Usage: " + sys.argv[0] + " <read fd> <write fd> <pid>")
        exit(1)

    read_fd = int(sys.argv[1])
    write_fd = int(sys.argv[2])
    pid = int(sys.argv[3])
    writer = os.fdopen(write_fd, "wb", 0)
    reader = os.fdopen(read_fd, "rb", 0)
    tester.run(reader, writer, pid)

if __name__ == '__main__':
    main()
