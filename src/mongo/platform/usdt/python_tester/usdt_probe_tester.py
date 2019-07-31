#!/usr/bin/env python3
""" This module can load a JSON configuration for testing eBPF probes and generate eBPF programs, attach them to the specified probes, and verify their output"""

import os
import sys
from tester import tester

if os.getuid() != 0:
    print("USDT tests must be run as sudo.")
    exit(1)


def main():
    """ parse command line args and run the tester """
    # parse command line args
    if len(sys.argv) < 3:
        print("Usage: " + sys.argv[0] + " <read fd> <write fd>")
        exit(1)

    reader = open(sys.argv[2], "rb", 0)
    writer = open(sys.argv[1], "wb", 0)
    test_passes = False
    try:
        tester.run(reader, writer)
        test_passes = True
    finally:
        writer.close()
        reader.close()

    if not test_passes:
        exit(1)


if __name__ == '__main__':
    main()
