#!/usr/bin/python3
""" This module can load a JSON configuration for testing eBPF probes and generate eBPF programs, attach them to the specified probes, and verify their output"""

import os
import sys
from tester import tester

if os.getuid() != 0:
    print("USDT tests must be run as sudo.")
    exit(0)

def main():
    """ parse command line args and run the tester """
    # parse command line args
    if len(sys.argv) < 3:
        print("Usage: " + sys.argv[0] + " <read fd> <write fd>")
        exit(1)

    reader = open(sys.argv[2], "rb")
    writer = open(sys.argv[1], "wb")
    try:
        tester.run(reader, writer)
    finally:
        writer.close();
        reader.close();

if __name__ == '__main__':
    main()
