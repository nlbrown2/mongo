/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "usdt_probe_test.h"

#include "mongo/platform/usdt.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"

void multipleEmptyProbesTest() {
    for (int i = 0; i < 15; i++) {
        MONGO_USDT(aProbe);
    }
}

void intProbesTest() {
    MONGO_USDT(probe1, 42);
    MONGO_USDT(probe2, 1, 2);
    MONGO_USDT(probe12, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
    for (int i = 0; i < 23; i++) {
        MONGO_USDT(probe1223,
                   i + 12,
                   i + 13,
                   i + 14,
                   i + 15,
                   i + 16,
                   i + 17,
                   i + 18,
                   i + 19,
                   i + 20,
                   i + 21,
                   i + 22,
                   i + 23);
    }
}

void strProbesTest() {
    MONGO_USDT(probeA, "albatross");
    MONGO_USDT(probeB, "bard", "cantaLoupe!");
    MONGO_USDT(probeC,
               "str0",
               "str1",
               "str2",
               "str3",
               "str4",
               "str5",
               "str6",
               "str7",
               "str8",
               "str9",
               "str10",
               "str11");
    MONGO_USDT(probeComplex, "hello, World!\n \"salut, monde!\"");
}

void structProbesTest() {
    struct {
        int i = 25;
        const char s[6] = "hello";
    } s;
    MONGO_USDT(basicStruct, &s);

    struct {
        int x = 333;
        struct {
            const char s[5] = "duck";
            int y = 22;
        } inner;
    } n;
    MONGO_USDT(nestedStruct, &n);

    struct {
        int i = 25;
    } justInt;
    struct {
        const char s[6] = "hello";
    } justStr;
    MONGO_USDT(multipleStruct, &justInt, &justStr);

    // to avoid -Wunused-variable warnings
    (void)s;
    (void)n;
    (void)justInt;
    (void)justStr;
}

void multipleStringStructTest() {
    struct {
        char str1[8] = "string1";
    } first;
    struct {
        char str2[8] = "string2";
        char str3[8] = "string3";
    } second;
    MONGO_USDT(multi_string, &first, &second);
}

USDT_PROBE_TEST_MAIN() {
    // multiple empty probes test
    ASSERT(tester.runTest(
        mongo::USDTProbe("aProbe", 15,  [](auto& res, int hit, auto& status) {}),
        multipleEmptyProbesTest)
    );

    // test INT args
    std::vector<mongo::USDTProbe> intProbes{
        mongo::USDTProbe("probe1",
                         1,
                         [](auto& res, int hit, auto& status) {
                             mongo::USDTProbeArg::expectEqualInts(res, 42, status);
                         })
            .withIntArg(),
        mongo::USDTProbe("probe2",
                         1,
                         [](auto& res, int hit, auto& status) {
                             mongo::USDTProbeArg::expectEqualInts(res, 1, status);
                             mongo::USDTProbeArg::expectEqualInts(res, 2, status);
                         })
            .withIntArgs(2),
        mongo::USDTProbe("probe12",
                         1,
                         [](auto& res, int hit, auto& status) {
                             for (int i = 12; i < 24; i++) {
                                 mongo::USDTProbeArg::expectEqualInts(res, i, status);
                             }
                         })
            .withIntArgs(12),
        mongo::USDTProbe("probe1223", 23, [](auto& res, int hit, auto& status) {
            for (int i = 12; i < 24; i++) {
                mongo::USDTProbeArg::expectEqualInts(res, i + hit, status);
            }
        }).withIntArgs(12)};

    ASSERT(tester.runTest(intProbes, intProbesTest));

    // ensure that passing along the wrong number is detected
    // for just one argument
    std::vector<mongo::USDTProbe> failures{
        mongo::USDTProbe("fails", 1, [](auto& res, int hit, auto& status) {
            mongo::USDTProbeArg::expectEqualInts(res, 42, status);
        }).withIntArg()};

    ASSERT_FALSE(tester.runTest(failures, []() { MONGO_USDT(fails, 4); }));

    // for many arguments
    std::vector<mongo::USDTProbe> failuresWithMany{
        mongo::USDTProbe("failsMany", 1, [](auto& res, int hit, auto& status) {
            mongo::USDTProbeArg::expectEqualInts(res, 42, status);
            mongo::USDTProbeArg::expectEqualInts(res, 43, status);
        }).withIntArgs(2)};

    ASSERT_FALSE(tester.runTest(failuresWithMany, []() { MONGO_USDT(failsMany, 42, 42); }));

    // test STRING args
    std::vector<mongo::USDTProbe> strProbes;
    strProbes.push_back(mongo::USDTProbe("probeA", 1, [](auto& res, int hit, auto& status) {
                            mongo::USDTProbeArg::expectEqualStrings(res, "albatross", status);
                        }).withStringArg(10));

    strProbes.push_back(
        mongo::USDTProbe("probeB",
                         1,
                         [](auto& res, int hit, auto& status) {
                             mongo::USDTProbeArg::expectEqualStrings(res, "bard", status);
                             mongo::USDTProbeArg::expectEqualStrings(res, "cantaLoupe!", status); 
                         })
            .withStringArg(5)
            .withStringArg(12));

    mongo::USDTProbe probe12Str("probeC", 1, [](auto& res, int hit, auto& status) {
        for (int i = 0; i < 12; i++) {
            std::stringstream nexts;
            nexts << "str" << i;
            mongo::USDTProbeArg::expectEqualStrings(res, nexts.str(), status);
        }
    });

    for (int i = 0; i < 12; i++) {
        probe12Str.withStringArg(i >= 10 ? 6 : 5);
    }

    strProbes.push_back(probe12Str);

    strProbes.push_back(mongo::USDTProbe("probeComplex", 1, [](auto& res, int hit, auto& status) {
                            mongo::USDTProbeArg::expectEqualStrings(res, "hello, World!\n \"salut, monde!\"", status);
                        }).withStringArg(34));

    ASSERT(tester.runTest(strProbes, strProbesTest));

    // test STRUCT args
    std::vector<mongo::USDTProbe> structProbes{
        mongo::USDTProbe("basicStruct",
                         1,
                         [](auto& res, int hit, auto& status) {
                             mongo::USDTProbeArg::expectEqualInts(res, 25, status);
                             mongo::USDTProbeArg::expectEqualStrings(res, "hello", status);
                         })
            .withArg(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT)
                         .withIntMember()
                         .withStringMember(6)),
        mongo::USDTProbe("nestedStruct",
                         1,
                         [](auto& res, int hit, auto& status) {
                             mongo::USDTProbeArg::expectEqualInts(res, 333, status);
                             mongo::USDTProbeArg::expectEqualStrings(res, "duck", status);
                             mongo::USDTProbeArg::expectEqualInts(res, 22, status);
                         })
            .withArg(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT)
                         .withIntMember()
                         .withMember(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT)
                                         .withStringMember(5)
                                         .withIntMember())),
        mongo::USDTProbe("multipleStruct",
                         1,
                         [](auto& res, int hit, auto& status) {
                             mongo::USDTProbeArg::expectEqualInts(res, 25, status); 
                             mongo::USDTProbeArg::expectEqualStrings(res, "hello", status);
                         })
            .withArg(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT).withIntMember())
            .withArg(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT).withStringMember(6))};

    ASSERT(tester.runTest(structProbes, structProbesTest));

    mongo::USDTProbe multipleStringStruct("multi_string", 1, [](auto& res, int hit, auto& status) {
        mongo::USDTProbeArg::expectEqualStrings(res, "string1", status);
        mongo::USDTProbeArg::expectEqualStrings(res, "string2", status);
        mongo::USDTProbeArg::expectEqualStrings(res, "string3", status);
    });

    multipleStringStruct
        .withArg(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT).withStringMember(8))
        .withArg(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT)
                     .withStringMember(8)
                     .withStringMember(8));

    ASSERT(tester.runTest(multipleStringStruct, multipleStringStructTest));

    // test POINTER args
    int throwaway = 5;
    void* ptr = &throwaway;
    ASSERT(tester.runTest(mongo::USDTProbe("ptrProbe",
                                           1,
                                           [ptr](auto& res, int hit, auto& status) {
                                               mongo::USDTProbeArg::expectEqualPtrs(res, ptr, status);
                                           })
                              .withPtrArg(),
                          [ptr]() { MONGO_USDT(ptrProbe, ptr); }));

    ASSERT(tester.runTest(
        mongo::USDTProbe("ptrStruct",
                         1,
                         [ptr](auto& res, int hit, auto& status) {
                            mongo::USDTProbeArg::expectEqualPtrs(res, ptr, status);
                         })
            .withArg(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT)
                         .withMember(mongo::USDTProbeArg(mongo::USDTProbeType::POINTER))),
        [ptr]() {
            struct {
                void* pointer;
            } tmp;
            tmp.pointer = ptr;
            MONGO_USDT(ptrStruct, &tmp);
        }));
}
