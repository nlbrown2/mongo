#!/usr/bin/python3
""" This module can load a JSON configuration for testing eBPF probes and generate eBPF programs, attach them to the specified probes, and verify their output"""

import json
import os
import sys
import traceback
from bcc import BPF, USDT
from generator import Generator, Probe
from util import STRING_TYPE, PROBE_NAME_KEY, PROBE_ARGS_KEY, ARG_TYPE_KEY


def _validate_json_args(args_obj, values_len):
    accepted_types = (STRING_TYPE, 'int', 'long', 'struct')
    for arg in args_obj:
        if arg[ARG_TYPE_KEY] not in accepted_types:
            raise JSONException('argument for probe {} has unsupported type {}'.format(probe[PROBE_NAME_KEY], arg[ARG_TYPE_KEY]))
        if arg[ARG_TYPE_KEY] == STRING_TYPE and (not arg.get("length") or not isinstance(arg["length"], int)):
            raise JSONException('string args must specify an int literal for length')
        val = arg.get("value")
        if not val:
            if not isinstance(arg["values"], list):
                raise JSONException('values must be specified as an array. To specify a constant, use the key "value"')
            if arg[ARG_TYPE_KEY] == 'struct':
                _validate_json_args(arg["values"], values_len)
            elif len(arg["values"]) != values_len:
                raise JSONException('values must specify a value for each hit. Expected {} values, saw {}'
                        .format(values_len, len(arg["values"])))

def validate_json(json_obj, writer):
    """ given a JSON object (dict), ensure it is a valid configuration """
    #pylint: disable=too-many-branches
    class JSONException(Exception):
        """custom exception class to handle JSON format errors"""
        pass

    try:
        if len(json_obj) != 1:
            raise JSONException('too many top level keys in JSON. Was only expecting "probes"')
        probes = json_obj["probes"] #will raise an exception if probes does not exist
        for probe in probes:
            if len(probe) != 3:
                raise JSONException(
                        'wrong number of keys for a probe. Was only expecting three: "name", "args" and "hits"'
                        )
            if not isinstance(probe[PROBE_NAME_KEY], str):
                raise JSONException(
                        'probe {} is supposed to have a string value for key {}. It has type {}'
                        .format(probe[PROBE_NAME_KEY], PROBE_NAME_KEY, type(probe[PROBE_NAME_KEY]))
                        )
            if not isinstance(probe[PROBE_ARGS_KEY], list):
                raise JSONException('probe is supposed to have an array value for key {}'.format(PROBE_ARGS_KEY))
            if not isinstance(probe["hits"], int):
                raise JSONException('probe is supposed to have an integer literal for key {}'.format("hits"))
            _validate_json_args(probe[PROBE_ARGS_KEY], probe["hits"])
    except (JSONException, KeyError) as ex:
        print("error parsing JSON configuration: ", str(ex))
        writer.write(b'j' if isinstance(ex, JSONException) else b'k')
        sys.exit(1)

def load_json(reader, writer):
    """reads json text of the specified size from a named pipe with name pipe_name and performs validation"""
    # first reads an integer that specifies the size of the JSON coming over the pipe
    line = str(reader.readline(), 'utf-8').strip()
    json_size = int(line)
    total_read = 0
    json_text = b''
    while total_read < json_size:
        read = reader.read(json_size)
        total_read += len(read)
        json_text += read

    # parse the json
    json_obj = json.loads(json_text)
    validate_json(json_obj, pipe_name)
    return json_obj

TESTS_FAILED = [] # keep track of which tests have failed
def callback_gen(bpf_obj, probe, probe_hit_counts):
    """ returns a function that can handle and validate the args passed to probe probe_name. Updates probe_hit_counts """
    def process_callback(cpu, data, size):
        """ on every event, this callback will trigger with new data. It will iterate over the specified args, validating each one """
        del cpu, size #these are unused
        passes = True
        for index, arg in enumerate(probe.args):
            try:
                validate_arg(bpf_obj[probe.name].event(data), arg, probe_hit_counts[probe.name])
            except (FailedCompException, IndexError) as error:
                passes = False
                if isinstance(error, FailedCompException):
                    print("Error validating args for probe: " + probe.name, file=sys.stderr)
                    print(str(error))
                    TESTS_FAILED.append((probe.name, probe_hit_counts[probe.name], index))
                else:
                    print("Error! probe {} hit too many times".format(probe.name))
                    TESTS_FAILED.append((probe.name, probe_hit_counts[probe.name])) # not arg specific, so drop index
                    break # no need to "fail" for every arg
        print(probe.name, ' iteration: {}...{}'.format(probe_hit_counts[probe.name], 'PASS' if passes else 'FAILED'))
        probe_hit_counts[probe.name] += 1
    return process_callback

# create and enable USDT objects
def attach_bpf(bpf_text, probes, pid, probe_hit_counts):
    """ creates usdt_probes and attaches them to a BPF object.
    Sets up BPF object callbacks. Returns the initalized BPF object"""
    usdt_probes = [USDT(pid=pid) for p in probes]
    for index, probe in enumerate(probes):
        usdt_probes[index].enable_probe(probe=probe.name, fn_name=probe.name + "_fn")

    bpf = BPF(text=bpf_text, usdt_contexts=usdt_probes)
    for probe in probes:
        bpf[probe.name].open_perf_buffer(callback_gen(bpf, probe, probe_hit_counts))

    return bpf

class FailedCompException(Exception):
    """Custom exception class for handling failed comparisons"""
    def __init__(self, expected, actual):
        Exception.__init__(self, str(expected) + ' != ' + str(actual))
        self.expected = expected
        self.actual = actual

    def __str__(self):
        base = Exception.__str__(self)
        base += '. Expected: {}. Got {}'.format(self.expected, self.actual)
        return base

def validate_arg(event, arg, hit_num):
    """given which channel to get an event from, this function will ensure
    the specified arg (arg_index and arg_type) hold the correct value"""
    if arg.type == 'struct':
        for arg in arg.values:
            validate_arg(event, arg, hit_num)
        return
    expected_val = arg.values[hit_num]
    actual = getattr(event, arg.output_arg_name)
    if arg.type == STRING_TYPE:
        expected_val = bytes(expected_val, 'utf-8') # strings are passed as bytestrings
    if str(actual) != str(expected_val):
        raise FailedCompException(expected_val, actual)

def expecting_more_probe_hits(probes, probe_hit_counts):
    """ determines if the testing program expects more probes to be hit or not """
    for probe in probes:
        if probe.hits > probe_hit_counts[probe.name]:
            return True
    return False

def main():
    #pylint: disable=missing-docstring
    # parse command line args
    if len(sys.argv) < 4:
        print("Usage: " + sys.argv[0] + " <write fd> <read fd> <pid>")
        exit(1)

    write_fd = int(sys.argv[1])
    read_fd = int(sys.argv[2])
    pid = int(sys.argv[3])
    writer = os.fdopen(write_fd, "wb", 0)
    reader = os.fdopen(read_fd, "rb", 0)

    json_obj = load_json(reader, writer)
    probes = json_obj["probes"]

    gen = Generator()
    probes = [Probe(probe) for probe in probes]
    for probe in probes:
        gen.add_probe(probe)
    bpf_text = gen.finish()
    print("BPF program: ")
    print(bpf_text)
    probe_hit_counts = {probe.name : 0 for probe in probes}
    bpf_obj = attach_bpf(bpf_text, probes, pid, probe_hit_counts)

    # tell unittest driver that we are ready
    writer.write(b'>')

    while expecting_more_probe_hits(probes, probe_hit_counts):
        bpf_obj.perf_buffer_poll() # failing tests will eventually time out

    # test if the probes are going to be hit one more time
    # the timeout is in miliseconds (same as syscall poll)
    bpf_obj.perf_buffer_poll(1000)

    print("\n\n\n==================SUMMARY=================")

    if TESTS_FAILED:
        print("FAILED!")
        for failure in TESTS_FAILED:
            if len(failure) == 3:
                print('{} failed when it was hit on the {} time for arg {}'.format(failure[0], failure[1], failure[2]))
            else:
                print('{} failed when it was hit on the {} time'.format(failure[0], failure[1]))
        sys.exit(1)

    print("SUCCEEDED")

if __name__ == '__main__':
    main()
