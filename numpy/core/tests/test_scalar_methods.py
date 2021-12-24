"""
Test the scalar constructors, which also do type-coercion
"""
import fractions
import platform
import types
from typing import Any, Type
from math import copysign

import pytest
import numpy as np

from numpy.testing import assert_equal, assert_raises, IS_MUSL


class TestAsIntegerRatio:
    # derived in part from the cpython test "test_floatasratio"

    @pytest.mark.parametrize("ftype", [
        np.half, np.single, np.double, np.longdouble])
    @pytest.mark.parametrize("f, ratio", [
        (0.875, (7, 8)),
        (-0.875, (-7, 8)),
        (0.0, (0, 1)),
        (11.5, (23, 2)),
        ])
    def test_small(self, ftype, f, ratio):
        assert_equal(ftype(f).as_integer_ratio(), ratio)

    @pytest.mark.parametrize("ftype", [
        np.half, np.single, np.double, np.longdouble])
    def test_simple_fractions(self, ftype):
        R = fractions.Fraction
        assert_equal(R(0, 1),
                     R(*ftype(0.0).as_integer_ratio()))
        assert_equal(R(5, 2),
                     R(*ftype(2.5).as_integer_ratio()))
        assert_equal(R(1, 2),
                     R(*ftype(0.5).as_integer_ratio()))
        assert_equal(R(-2100, 1),
                     R(*ftype(-2100.0).as_integer_ratio()))

    @pytest.mark.parametrize("ftype", [
        np.half, np.single, np.double, np.longdouble])
    def test_errors(self, ftype):
        assert_raises(OverflowError, ftype('inf').as_integer_ratio)
        assert_raises(OverflowError, ftype('-inf').as_integer_ratio)
        assert_raises(ValueError, ftype('nan').as_integer_ratio)

    def test_against_known_values(self):
        R = fractions.Fraction
        assert_equal(R(1075, 512),
                     R(*np.half(2.1).as_integer_ratio()))
        assert_equal(R(-1075, 512),
                     R(*np.half(-2.1).as_integer_ratio()))
        assert_equal(R(4404019, 2097152),
                     R(*np.single(2.1).as_integer_ratio()))
        assert_equal(R(-4404019, 2097152),
                     R(*np.single(-2.1).as_integer_ratio()))
        assert_equal(R(4728779608739021, 2251799813685248),
                     R(*np.double(2.1).as_integer_ratio()))
        assert_equal(R(-4728779608739021, 2251799813685248),
                     R(*np.double(-2.1).as_integer_ratio()))
        # longdouble is platform dependent

    @pytest.mark.parametrize("ftype, frac_vals, exp_vals", [
        # dtype test cases generated using hypothesis
        # first five generated cases per dtype
        (np.half, [0.0, 0.01154830649280303, 0.31082276347447274,
                   0.527350517124794, 0.8308562335072596],
                  [0, 1, 0, -8, 12]),
        (np.single, [0.0, 0.09248576989263226, 0.8160498218131407,
                     0.17389442853722373, 0.7956044195067877],
                    [0, 12, 10, 17, -26]),
        (np.double, [0.0, 0.031066908499895136, 0.5214135908877832,
                     0.45780736035689296, 0.5906586745934036],
                    [0, -801, 51, 194, -653]),
        pytest.param(
            np.longdouble,
            [0.0, 0.20492557202724854, 0.4277180662199366, 0.9888085019891495,
             0.9620175814461964],
            [0, -7400, 14266, -7822, -8721],
            marks=[
                pytest.mark.skipif(
                    np.finfo(np.double) == np.finfo(np.longdouble),
                    reason="long double is same as double"),
                pytest.mark.skipif(
                    platform.machine().startswith("ppc"),
                    reason="IBM double double"),
            ]
        )
    ])
    def test_roundtrip(self, ftype, frac_vals, exp_vals):
        for frac, exp in zip(frac_vals, exp_vals):
            f = np.ldexp(ftype(frac), exp)
            assert f.dtype == ftype
            n, d = f.as_integer_ratio()

            try:
                nf = np.longdouble(n)
                df = np.longdouble(d)
                if not np.isfinite(df):
                    raise OverflowError
            except (OverflowError, RuntimeWarning):
                # the values may not fit in any float type
                pytest.skip("longdouble too small on this platform")

            assert_equal(nf / df, f, "{}/{}".format(n, d))


class TestIsInteger:
    @pytest.mark.parametrize("str_value", ["inf", "nan"])
    @pytest.mark.parametrize("code", np.typecodes["Float"])
    def test_special(self, code: str, str_value: str) -> None:
        cls = np.dtype(code).type
        value = cls(str_value)
        assert not value.is_integer()

    @pytest.mark.parametrize(
        "code", np.typecodes["Float"] + np.typecodes["AllInteger"]
    )
    def test_true(self, code: str) -> None:
        float_array = np.arange(-5, 5).astype(code)
        for value in float_array:
            assert value.is_integer()

    @pytest.mark.parametrize("code", np.typecodes["Float"])
    def test_false(self, code: str) -> None:
        float_array = np.arange(-5, 5).astype(code)
        float_array *= 1.1
        for value in float_array:
            if value == 0:
                continue
            assert not value.is_integer()


class TestClassGetItem:
    @pytest.mark.parametrize("cls", [
        np.number,
        np.integer,
        np.inexact,
        np.unsignedinteger,
        np.signedinteger,
        np.floating,
    ])
    def test_abc(self, cls: Type[np.number]) -> None:
        alias = cls[Any]
        assert isinstance(alias, types.GenericAlias)
        assert alias.__origin__ is cls

    def test_abc_complexfloating(self) -> None:
        alias = np.complexfloating[Any, Any]
        assert isinstance(alias, types.GenericAlias)
        assert alias.__origin__ is np.complexfloating

    @pytest.mark.parametrize("arg_len", range(4))
    def test_abc_complexfloating_subscript_tuple(self, arg_len: int) -> None:
        arg_tup = (Any,) * arg_len
        if arg_len in (1, 2):
            assert np.complexfloating[arg_tup]
        else:
            match = f"Too {'few' if arg_len == 0 else 'many'} arguments"
            with pytest.raises(TypeError, match=match):
                np.complexfloating[arg_tup]

    @pytest.mark.parametrize("cls", [np.generic, np.flexible, np.character])
    def test_abc_non_numeric(self, cls: Type[np.generic]) -> None:
        with pytest.raises(TypeError):
            cls[Any]

    @pytest.mark.parametrize("code", np.typecodes["All"])
    def test_concrete(self, code: str) -> None:
        cls = np.dtype(code).type
        with pytest.raises(TypeError):
            cls[Any]

    @pytest.mark.parametrize("arg_len", range(4))
    def test_subscript_tuple(self, arg_len: int) -> None:
        arg_tup = (Any,) * arg_len
        if arg_len == 1:
            assert np.number[arg_tup]
        else:
            with pytest.raises(TypeError):
                np.number[arg_tup]

    def test_subscript_scalar(self) -> None:
        assert np.number[Any]


class TestBitCount:
    # derived in part from the cpython test "test_bit_count"

    @pytest.mark.parametrize("itype", np.sctypes['int']+np.sctypes['uint'])
    def test_small(self, itype):
        for a in range(max(np.iinfo(itype).min, 0), 128):
            msg = f"Smoke test for {itype}({a}).bit_count()"
            assert itype(a).bit_count() == bin(a).count("1"), msg

    def test_bit_count(self):
        for exp in [10, 17, 63]:
            a = 2**exp
            assert np.uint64(a).bit_count() == 1
            assert np.uint64(a - 1).bit_count() == exp
            assert np.uint64(a ^ 63).bit_count() == 7
            assert np.uint64((a - 1) ^ 510).bit_count() == exp - 8

class TestFloatHex:
    # derived in part from the cpython test "HexFloatTestCase"

    def identical(self, x, y):
        # check that floats x and y are identical, or that both
        # are NaNs
        if np.isnan(x) or np.isnan(y):
            if np.isnan(x) == np.isnan(y):
                return True
        elif x == y and (
                x != 0.0 or np.copysign(1.0, x) == np.copysign(1.0, y)
            ):
            return True

        return False

    def roundtrip(self, x):
        return x.fromhex(x.hex())

    @pytest.mark.parametrize("ftype", np.sctypes['float'])
    def test_roundtrip(self, ftype):
        finfo = np.finfo(ftype)
        fltMax, fltMin = finfo.max, finfo.min
        fltEps, fltTiny = finfo.eps, finfo.tiny

        for x in [fltMax, fltMin, fltEps, fltTiny, 0.0, np.nan, np.inf]:
            assert self.identical(x, self.roundtrip(x))

    @pytest.mark.parametrize("ftype", np.sctypes['float'])
    def test_invalid_inputs(self, ftype):
        invalid_inputs = [
            'infi',  # misspelt infinities and nans
            '-Infinit',
            '++inf',
            '-+Inf',
            '--nan',
            '+-NaN',
            'snan',
            'NaNs',
            'nna',
            'an',
            'nf',
            'nfinity',
            'inity',
            'iinity',
            '0xnan',
            '',
            ' ',
            'x1.0p0',
            '0xX1.0p0',
            '+ 0x1.0p0',  # internal whitespace
            '- 0x1.0p0',
            '0 x1.0p0',
            '0x 1.0p0',
            '0x1 2.0p0',
            '+0x1 .0p0',
            '0x1. 0p0',
            '-0x1.0 1p0',
            '-0x1.0 p0',
            '+0x1.0p +0',
            '0x1.0p -0',
            '0x1.0p 0',
            '+0x1.0p+ 0',
            '-0x1.0p- 0',
            '++0x1.0p-0',  # double signs
            '--0x1.0p0',
            '+-0x1.0p+0',
            '-+0x1.0p0',
            '0x1.0p++0',
            '+0x1.0p+-0',
            '-0x1.0p-+0',
            '0x1.0p--0',
            '0x1.0.p0',
            '0x.p0',  # no hex digits before or after point
            '0x1,p0',  # wrong decimal point character
            '0x1pa',
            '0x1p\uff10',  # fullwidth Unicode digits
            '\uff10x1p0',
            '0x\uff11p0',
            '0x1.\uff10p0',
            '0x1p0 \n 0x2p0',
            '0x1p0\0 0x1p0',  # embedded null byte is not end of string
            ]
        for x in invalid_inputs:
            with pytest.raises(ValueError):
                result = ftype.fromhex(x)

    @pytest.mark.parametrize("ftype", np.sctypes['float'])
    @pytest.mark.parametrize("value_pairs", [
            ('inf', np.inf),
            ('-Infinity', -np.inf),
            ('nan', np.nan),
            ('1.0', 1.0),
            ('-0x.2', -0.125),
            ('-0.0', -0.0)
        ])
    @pytest.mark.parametrize("lead", [
            '', ' ', '\t', '\n', '\n \t', '\f', '\v', '\r'
        ])
    @pytest.mark.parametrize("trail", [
            '', ' ', '\t', '\n', '\n \t', '\f', '\v', '\r'
        ])
    def test_whitespace(self, ftype, value_pairs, lead, trail):
        inp, expected = value_pairs
        got = ftype.fromhex(lead + inp + trail)
        assert self.identical(got, expected)
