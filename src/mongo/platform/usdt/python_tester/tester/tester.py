""" This module is responsible for coordinating with the c++ bridge to run all needed tests """

# class Tester:
#     """ communicates with a bridge to run multiple USDT tests """

#     def __init__(self, bridge):
#         """ set up ourselves to communicate over the bridge """
#         self.bridge = bridge

#     def run(self, bridge):
#         """ reads a json spec from the bridge, attaches probes, and writes the captured values back to the C++ side """
#         json = bridge.get_json()
#         if not json:
#             return None
#         attached_probes = self.attach_probes(json)
#         if not attached_probes:
#             print("Could not attach probes! Error: {}".format(attached_probes.error_msg))
#         bridge.signal_ready_to_test()
#         self.test()
#         bridge.send_results(self.probes, self.probe_values)
#         return True

#     def attach_probes(self, json_spec):
#         probes = json_obj["probes"]

#         gen = Generator()
#         self.probes = [Probe(probe) for probe in probes]
#         for probe in probes:
#             gen.add_probe(probe)
#         self.bpf_text = gen.finish()
#         self.probe_hit_counts = {probe.name : 0 for probe in probes}
#         self.probe_values = {probe.name : [] for probe in probes}
#         self.bpf_obj = self.attach_bpf(bpf_text, probes, pid, probe_hit_counts, probe_values)
#         return True

#     def test(self):
#         while self.expecting_more_probe_hits():
#             self.bpf_obj.perf_buffer_poll() # wait for another probe to be hit
import json
from bcc import BPF, USDT
from .util import *
from .generator import *
def _validate_json_args(args_obj):
    accepted_types = (STRING_TYPE, 'int', 'long', 'struct')
    for arg in args_obj:
        if arg[ARG_TYPE_KEY] not in accepted_types:
            raise JSONException('argument for probe {} has unsupported type {}'.format(probe[PROBE_NAME_KEY], arg[ARG_TYPE_KEY]))
        if arg[ARG_TYPE_KEY] == STRING_TYPE and (not arg.get("length") or not isinstance(arg["length"], int)):
            raise JSONException('string args must specify an int literal for length')
        if arg.get(ARG_TYPE_KEY) != 'struct' and (arg.get("value") or arg.get("values")):
            raise JSONException("Deprecated use of value[s]")
        # val = arg.get("value")
        # if not val:
        #     if not isinstance(arg["values"], list):
        #         raise JSONException('values must be specified as an array. To specify a constant, use the key "value"')
        #     if arg[ARG_TYPE_KEY] == 'struct':
        #         _validate_json_args(arg["values"], values_len)
        #     elif len(arg["values"]) != values_len:
        #         raise JSONException('values must specify a value for each hit. Expected {} values, saw {}'
        #                 .format(values_len, len(arg["values"])))

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
            _validate_json_args(probe[PROBE_ARGS_KEY])
    except (JSONException, KeyError) as ex:
        print("error parsing JSON configuration: ", str(ex))
        writer.write(b'j' if isinstance(ex, JSONException) else b'k')
        sys.exit(1)

def load_json(reader, writer):
    """reads json text of the specified size from a named pipe with name writer and performs validation"""
    # first reads an integer that specifies the size of the JSON coming over the pipe
    line = str(reader.readline(), 'utf-8').strip()
    json_size = int(line)
    if json_size is 0:
        return None
    total_read = 0
    json_text = b''
    while total_read < json_size:
        read = reader.read(json_size)
        total_read += len(read)
        json_text += read

    # parse the json
    json_obj = json.loads(json_text)
    validate_json(json_obj, writer)
    return json_obj

TESTS_FAILED = [] # keep track of which tests have failed
def callback_gen(bpf_obj, probe, probe_hit_counts, output_arr):
    """ returns a function that can handle and validate the args passed to probe probe_name. Updates probe_hit_counts """
    def process_callback(cpu, data, size):
        """ on every event, this callback will trigger with new data. It will iterate over the specified args, validating each one """
        del cpu, size #these are unused
        passes = True
        to_send = ''
        for index, arg in enumerate(probe.args):
            try:
                to_send += stringify_arg(bpf_obj[probe.name].event(data), arg)
            except IndexError as error:
                passes = False
                print("Error! probe {} hit too many times".format(probe.name))
                TESTS_FAILED.append((probe.name, probe_hit_counts[probe.name])) # not arg specific, so drop index
                break # no need to "fail" for every arg
        to_send += '\n'
        output_arr.append(to_send)
        #print(probe.name, ' iteration: {}...{}'.format(probe_hit_counts[probe.name], 'PASS' if passes else 'FAILED'))
        probe_hit_counts[probe.name] += 1
    return process_callback

# create and enable USDT objects
def attach_bpf(bpf_text, probes, pid, probe_hit_counts, output_arrays):
    """ creates usdt_probes and attaches them to a BPF object.
    Sets up BPF object callbacks. Returns the initalized BPF object"""
    usdt_probes = [USDT(pid=pid) for p in probes]
    for index, probe in enumerate(probes):
        usdt_probes[index].enable_probe(probe=probe.name, fn_name=probe.name + "_fn")

    bpf = BPF(text=bpf_text, usdt_contexts=usdt_probes)
    for probe in probes:
        bpf[probe.name].open_perf_buffer(callback_gen(bpf, probe, probe_hit_counts, output_arrays[probe.name]))

    return bpf

def stringify_arg(event, arg):
    """returns the value for the provided argument within the event as a stringified representation"""
    res = ''
    if arg.type == 'struct':
        for arg in arg.values:
            res += stringify_arg(event, arg)
    else:
        actual = getattr(event, arg.output_arg_name)
        if arg.type == STRING_TYPE:
            res += '"' + str(actual, 'utf-8').replace('"', '\\"') + '" '
        else:
            res += str(actual) + ' '
    #print(arg, res)
    return res

def expecting_more_probe_hits(probes, probe_hit_counts):
    """ determines if the testing program expects more probes to be hit or not """
    for probe in probes:
        if probe.hits > probe_hit_counts[probe.name]:
            return True
    return False

def run(reader, writer, pid):
    while True:
        writer.write(b'>')
        json_obj = load_json(reader, writer)
        if not json_obj:
            print("All tests have run\n")
            break
        print("==================JSON========================\n{}\n".format(json_obj))
        probes = json_obj["probes"]

        gen = Generator()
        probes = [Probe(probe) for probe in probes]
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

if __name__ == '__main__':
    main()
