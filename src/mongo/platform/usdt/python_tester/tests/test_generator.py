import sys
import os
import unittest
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from tester import generator, util

ARG_TYPES = [util.INT_TYPE, util.STRING_TYPE, util.STRUCT_TYPE, util.POINTER_TYPE]


def get_arg_dict(arg_type, length=None):
    base = {
        util.ARG_TYPE_KEY: arg_type,
    }
    if length:
        base[util.ARG_STR_LEN_KEY] = length
    return base


def attach_struct_field(struct_dict, arg_dict):
    current_fields = struct_dict.get(util.ARG_STRUCT_FIELDS_KEY, [])
    current_fields.append(arg_dict)
    struct_dict[util.ARG_STRUCT_FIELDS_KEY] = current_fields
    return struct_dict


def args_are_equal(arg1, arg2):
    if arg1.type != arg2.type:
        return False
    if arg1.probe_name != arg2.probe_name:
        return False
    if arg1.depth != arg2.depth:
        return False
    if arg1.index != arg2.index:
        return False
    if getattr(arg1, util.ARG_STR_LEN_KEY, None) != getattr(arg2, util.ARG_STR_LEN_KEY, None):
        return False
    if hasattr(arg1, util.ARG_STRUCT_FIELDS_KEY):
        first_fields = getattr(arg1, util.ARG_STRUCT_FIELDS_KEY)
        second_fields = getattr(arg2, util.ARG_STRUCT_FIELDS_KEY)
        for index in range(len(first_fields)):
            if not args_are_equal(first_fields[index], second_fields[index]):
                return False
    return True


class TestGeneratorProbeCtor(unittest.TestCase):
    def setUp(self):
        self.probe_name = 'test_probe_name'
        self.hits = 42
        self.args = []

    def validate_fields(self, probe):
        self.assertTrue(probe.function_name)
        self.assertTrue(isinstance(probe.hits, int))
        self.assertTrue(probe.name)

    def get_probe_dict(self):
        return {
            util.PROBE_NAME_KEY: self.probe_name, util.PROBE_HIT_KEY: self.hits,
            util.PROBE_ARGS_KEY: self.args
        }

    def test_no_args_positive(self):
        assert not len(self.args)
        p = generator.Probe(self.get_probe_dict())
        self.assertEqual(self.probe_name, p.name)
        self.assertEqual(self.hits, p.hits)
        self.assertListEqual(p.args, self.args)
        self.validate_fields(p)

    def test_invalid_hits_negative(self):
        self.hits = '42'
        with self.assertRaises(AssertionError):
            p = generator.Probe(self.get_probe_dict())

    def test_missing_args_negative(self):
        probe_dict = self.get_probe_dict()
        probe_dict.pop(util.PROBE_ARGS_KEY)
        with self.assertRaises(KeyError):
            p = generator.Probe(probe_dict)

    def test_one_arg_positive(self):
        # add helper function to generate arg dict
        self.args.append(get_arg_dict(util.STRING_TYPE, 42))
        p = generator.Probe(self.get_probe_dict())
        arg = generator.Arg(self.args[0], p.hits, p.name, 0)
        self.assertEqual(len(p.args), len(self.args))
        self.assertTrue(args_are_equal(arg, p.args[0]))
        self.validate_fields(p)

    def test_many_args_positive(self):
        self.args.append(get_arg_dict(util.INT_TYPE))
        self.args.append(get_arg_dict(util.STRING_TYPE, 50))
        struct_field = get_arg_dict(util.INT_TYPE)
        struct_dict = get_arg_dict(util.STRUCT_TYPE)
        struct_dict = attach_struct_field(struct_dict, struct_field)
        self.args.append(struct_dict)

        p = generator.Probe(self.get_probe_dict())
        self.args = [
            generator.Arg(arg_dict, p.hits, p.name, index)
            for index, arg_dict in enumerate(self.args)
        ]
        for correct_arg, probe_arg in zip(self.args, p.args):
            self.assertTrue(args_are_equal(correct_arg, probe_arg))
        self.validate_fields(p)


class TestGeneratorArgCName(unittest.TestCase):
    def setUp(self):
        self.probe_name = 'no_actual_probe'
        self.hits = 2
        self.index = 1

        self.int = generator.Arg(
            get_arg_dict(util.INT_TYPE), self.hits, self.probe_name, self.index)
        self.length = 42
        self.str = get_arg_dict(util.STRING_TYPE, self.length)
        self.struct = attach_struct_field(
            attach_struct_field(get_arg_dict(util.STRUCT_TYPE), get_arg_dict(util.INT_TYPE)),
            self.str)
        print(self.struct)
        self.struct = generator.Arg(self.struct, self.hits, self.probe_name, self.index)

    def _test_type_c_type(self, arg, type_):
        self.assertEqual('{} {}'.format(type_, arg.output_arg_name), arg.get_c_decl())

    def test_int_c_type(self):
        self._test_type_c_type(self.int, 'int')


# These tests were left incomplete in pursuit of less fragile, more meaningful end-to-end tests

if __name__ == "__main__":
    unittest.main()
