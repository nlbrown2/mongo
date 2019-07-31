#!/usr/bin/env python3
"""This module can load a JSON configuration for testing eBPF probes and generate
eBPF programs, attach them to the specified probes, and send the output for validation.
"""
import argparse
import os
import sys
from tester import tester

if os.getuid() != 0:
    print("USDT tests must be run as sudo.")
    exit(1)


def main():
    """ parse command line args and run the tester """
    parser = argparse.ArgumentParser()
    parser.add_argument("write_fifo_name")
    parser.add_argument("read_fifo_name")
    args = parser.parse_args()

    # have to open reader then writer to match the C++ process opening the other side
    with open(args.read_fifo_name, "rb", 0) as reader, open(args.write_fifo_name, "wb",
                                                            0) as writer:
        test_passes = False
        try:
            tester.run(reader, writer)
            test_passes = True
        finally:
            if not test_passes:
                exit(1)


if __name__ == '__main__':
    main()
