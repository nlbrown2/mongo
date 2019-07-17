""" module that can generate c-style eBPF programs given a list of probes to attach to and their arguments """
from .util import STRING_TYPE, STRUCT_TYPE, PROBE_NAME_KEY, PROBE_ARGS_KEY, ARG_TYPE_KEY

PROBE_HIT_KEY = "hits"
ARG_STR_LEN_KEY = "length"
ARG_STRUCT_FIELDS_KEY = "fields"

class Probe:
    """ representation of a probe specification useful for code generation """
    def __init__(self, probe_dict):
        assert isinstance(probe_dict, dict)

        self.name = probe_dict[PROBE_NAME_KEY]
        assert isinstance(self.name, str)

        self.hits = probe_dict[PROBE_HIT_KEY]
        assert isinstance(self.hits, int)

        self.args = [Arg(arg, self.hits, self.name, index) for index, arg in enumerate(probe_dict[PROBE_ARGS_KEY])]
        self.function_name = self.name + '_fn'

class Arg:
    """ Representation of an argument to a Probe holding information about where it can be located """
    def __init__(self, arg_dict, num_hits, probe_name, index, depth=0):
        assert isinstance(arg_dict, dict)

        self.type = arg_dict[ARG_TYPE_KEY]
        assert self.type in (STRING_TYPE, STRUCT_TYPE, 'int', 'long')

        self.probe_name = probe_name
        assert isinstance(self.probe_name, str)

        self.depth = depth
        assert isinstance(self.depth, int)

        self.index = index
        assert isinstance(self.index, int)

        self.output_arg_name = 'arg_{}_{}'.format(self.depth, self.index)

        if self.type == STRING_TYPE:
            self.length = arg_dict[ARG_STR_LEN_KEY]
            assert isinstance(self.length, int)

        if self.type == STRUCT_TYPE:
            self.fields = [Arg(val, num_hits, probe_name, child_index, depth=depth+1) for child_index, val in enumerate(arg_dict[ARG_STRUCT_FIELDS_KEY])]
            for field in self.fields:
                field.output_arg_name += '_{}'.format(self.index)

    def get_c_name(self):
        """ return the type and name of this argument in a C program. The name should be unique to an instance but the same across instances """
        if self.type == STRING_TYPE:
            return 'char {arg_name}[{length}]'.format(arg_name=self.output_arg_name, length=self.length)
        elif self.type == STRUCT_TYPE:
            return 'struct {probe_name}_level_{depth}_{index} {arg_name}'.format(probe_name=self.probe_name, depth=self.depth, arg_name=self.output_arg_name, index=self.index)
        else:
            return '{type_} {arg_name}'.format(type_=self.type, arg_name=self.output_arg_name)

    def before_output_gen(self):
        """ Returns a string of any code that needs to be emitted before the output struct containing this argument is emitted.
            This allows structs to print their definitions (any any nested definitions) before they are referenced."""
        if self.type != STRUCT_TYPE:
            return ''
        result = ''
        for member in self.fields:
            if member.type == STRUCT_TYPE:
                result += member.before_output_gen()

        result += 'struct {probe_name}_level_{depth}_{index} {{\n'.format(probe_name=self.probe_name, depth=self.depth, index=self.index)
        for member in self.fields:
            result += '\t' + member.get_c_name() + ';\n'
        result += '};\n'
        return result

    def get_output_struct_def(self):
        """ returns what members in the output struct this arg is responsible for. """
        if self.type != STRUCT_TYPE:
            return '\t' + self.get_c_name() + ';\n'
        result = ''
        for arg in self.fields:
            result += arg.get_output_struct_def()
        return result

    def fill_output_struct(self, source_struct_name=None):
        """ returns the code necessary to fill the members of the output struct this arg is responsible for """
        if source_struct_name:
            # we should be reading our value out of a struct
            if self.type == STRING_TYPE:
                # Can't access C runtime (strcpy, etc) and there are no loops.
                # Thus, to copy strings from embedded structs to the output struct, the offset within the passed in struct
                # is determined, and then a bpf_probe_read_str is issued, reading the string from userspace once more
                # See https://github.com/iovisor/bcc/issues/691 for updates on string builtin functions
                result = """
\tconst char* {str_addr_name} = {source};
\tbpf_probe_read_str(out.{target}, sizeof(out.{target}), {str_addr_name});
""".format(source=source_struct_name + '.' + self.output_arg_name,
                        target=self.output_arg_name,
                        str_addr_name=self.output_arg_name.replace('arg', 'addr'))
                return result
            elif self.type == STRUCT_TYPE:
                # add ourselves to the source struct name for our fields to read out of
                result = ''
                source_struct_name += '.' + self.output_arg_name
                for arg in self.fields:
                    result += arg.fill_output_struct(source_struct_name) + '\n'
                return result
            return '\tout.{target} = {source}; //here1\n'.format(target=self.output_arg_name, source=source_struct_name + '.' + self.output_arg_name)
        elif self.type == STRING_TYPE:
            assert self.depth == 0
            # read the addr out of the USDT arg and read the C-string into our output struct
            return """
\tconst char* {addr_name} = NULL;
\tbpf_usdt_readarg({arg_num}, ctx, &{addr_name});
\tbpf_probe_read_str(&out.{out_member}, sizeof(out.{out_member}), {addr_name});
""".format(arg_num=self.index + 1, addr_name=self.output_arg_name.replace('arg', 'addr'), out_member=self.output_arg_name)
        elif self.type == STRUCT_TYPE:
            assert self.depth == 0
            # read in the struct from the USDT arg pointer
            struct_name = '{probe_name}_level_0_{index}_base'.format(probe_name=self.probe_name, index=self.index)
            result = """
\tstruct {probe_name}_level_0_{index} {struct_name} = {{}};
\tconst void* addr_{index} = NULL;
\tbpf_usdt_readarg({arg_num}, ctx, &addr_{index});
\tbpf_probe_read(&{struct_name}, sizeof({struct_name}), addr_{index});
""".format(probe_name=self.probe_name, index=self.index, struct_name=struct_name, arg_num=self.index + 1)
            for arg in self.fields:
                result += arg.fill_output_struct(struct_name) + '\n'
            return result
        else:
            # read the argument directly
            return '\tbpf_usdt_readarg({}, ctx, &out.{});\n'.format(self.index + 1, self.output_arg_name)

class Generator:
    """ Responsible for orchestrating the generation of code for each probe that gets added to it """
    def __init__(self):
        self.c_prog = '#include <linux/ptrace.h>\n'

    def finish(self):
        """ Do any clean up work and then provide the generated C program """
        return self.c_prog

    def add_probe(self, probe):
        """ add a probe and generate code to attach that probe to its own output channel and function """
        if not isinstance(probe, Probe):
            raise TypeError("Probe must be a dict!")
        for arg in probe.args:
            if arg.type == STRUCT_TYPE:
                self.c_prog += arg.before_output_gen()

        self.c_prog += "\n\nBPF_PERF_OUTPUT({});\n".format(probe.name)
        self.c_prog += "\nstruct {}_output {{\n".format(probe.name)
        for  arg in probe.args:
            self.c_prog += arg.get_output_struct_def()
        self.c_prog += '};\n\n'

        self.c_prog += 'int {}(struct pt_regs *ctx) {{\n'.format(probe.function_name)
        self.c_prog += '\tstruct {}_output out = {{}};\n'.format(probe.name)
        for arg in probe.args:
            self.c_prog += arg.fill_output_struct()
        self.c_prog += '\t{}.perf_submit(ctx, &out, sizeof(out));\n'.format(probe.name)
        self.c_prog += '\treturn 0;\n}\n'
