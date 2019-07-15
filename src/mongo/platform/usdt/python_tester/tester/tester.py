""" This module is responsible for coordinating with the c++ bridge to run all needed tests """

import json
from bcc import BPF, USDT
from .util import STRING_TYPE
from .generator import Probe, Arg, Generator

PROBE_ARRAY_KEY = "probes"
PID_KEY = 'pid'

def load_json(reader):
    """ parses the json it reads in from reader. Returns a list of generator.Probe objects or an int (if given a pid) """
    # first reads an integer that specifies the size of the JSON coming over the pipe
    line = str(reader.readline(), 'utf-8').strip()
    json_size = int(line)
    if json_size == 0:
        return None
    total_read = 0
    json_text = b''
    while total_read < json_size:
        read = reader.read(json_size)
        total_read += len(read)
        json_text += read

    json_obj = json.loads(json_text)
    # if given the PID body, return an int corresponding to that
    if PID_KEY in json_obj:
        return int(json_obj[PID_KEY])
    return [Probe(p) for p in json_obj[PROBE_ARRAY_KEY]]

def callback_gen(bpf_obj, probe, probe_hit_counts, output_arr):
    """ returns a function that can handle and validate the args passed to probe probe_name. Updates probe_hit_counts """
    def process_callback(cpu, data, size):
        """ on every event, this callback will trigger with new data. It will iterate over the specified args, validating each one """
        del cpu, size
        value_string = ''
        event = bpf_obj[probe.name].event(data)
        for arg in probe.args:
            value_string += stringify_arg(event, arg)
        value_string += '\n'
        output_arr.append(value_string)
        probe_hit_counts[probe.name] += 1
    return process_callback

def event_dropped_gen(probe):
    """ returns a function to be called when a probe is dropped """
    def event_dropped(arg):
        print("WARNING: {} DROPPED.", probe.name)
    return event_dropped

def attach_bpf(bpf_text, probes, pid, probe_hit_counts, output_arrays):
    """ creates usdt_probes and attaches them to a BPF object.
    Sets up BPF object callbacks. Returns the initalized BPF object"""
    usdt_probes = [USDT(pid=pid) for p in probes]
    for index, probe in enumerate(probes):
        usdt_probes[index].enable_probe(probe=probe.name, fn_name=probe.function_name)

    bpf = BPF(text=bpf_text, usdt_contexts=usdt_probes)
    for probe in probes:
        probeCallback = callback_gen(bpf, probe, probe_hit_counts, output_arrays[probe.name])
        bpf[probe.name].open_perf_buffer(probeCallback, lost_cb=event_dropped_gen(probe))

    return bpf

def stringify_arg(event, arg):
    """returns the value for the provided argument within the event as a stringified representation"""
    res = ''
    if arg.type == 'struct':
        for field in arg.fields:
            res += stringify_arg(event, field)
    else:
        actual = getattr(event, arg.output_arg_name)
        if arg.type == STRING_TYPE:
            res += '"' + str(actual, 'utf-8').replace('"', '\\"') + '"'
        else:
            res += str(actual)
    res += ' '
    return res

def expecting_more_probe_hits(probes, probe_hit_counts):
    """ determines if the testing program expects more probes to be hit or not """
    for probe in probes:
        if probe.hits > probe_hit_counts[probe.name]:
            return True
    return False

def run(reader, writer):
    """ run through all the tests specified by reader and provide results to writer """
    pid = -1
    while True:
        writer.write(b'>')
        probes = load_json(reader)
        if not probes:
            print("All tests have run\n")
            break
        elif isinstance(probes, int):
            pid = probes
            assert pid != -1
            continue

        gen = Generator()
        for probe in probes:
            gen.add_probe(probe)
        bpf_text = gen.finish()
        print("==================BPF PROGRAM=================")
        print(bpf_text)
        probe_hit_counts = {probe.name : 0 for probe in probes}
        probe_values = {probe.name : [] for probe in probes}
        bpf_obj = attach_bpf(bpf_text, probes, pid, probe_hit_counts, probe_values)

        # tell unittest driver that we have finished attaching our probes
        writer.write(b'>')

        while expecting_more_probe_hits(probes, probe_hit_counts):
            bpf_obj.perf_buffer_poll() # failing tests will eventually time out

        # test if the probes are going to be hit one more time
        # the timeout is in miliseconds (same as syscall poll)
        bpf_obj.perf_buffer_poll(1000)

        for probe in probes:
            writer.write(bytes(probe.name, 'utf-8'))
            writer.write(b'\n')
            for val in probe_values[probe.name]:
                bytesVal = bytes(val, 'utf-8')
                writer.write(bytes(str(len(bytesVal)), 'utf-8'))
                writer.write(b'\n')
                writer.write(bytesVal)

        print("\n\n\n==================SUMMARY====================")
        print("SUCCEEDED: All iterations for probes were registered.")
        bpf_obj.cleanup()
