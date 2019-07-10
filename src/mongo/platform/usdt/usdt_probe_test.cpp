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

#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "mongo/base/parse_number.h"
#include "mongo/platform/usdt.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"

namespace mongo {

std::string USDTProbeArg::getNextAsString(const std::string& in) {
    std::stringstream ss(in);
    return getNextAsString(ss);
}

std::string USDTProbeArg::getNextAsString(std::stringstream& in) {
    std::stringstream ssOut;
    char c;

    // toss out whitespace
    in >> std::ws;
    in >> c;
    ASSERT(c == '"');

    while (in.get(c)) {
        if (c == '\\' && in.peek() == '"') {
            in >> c;
            ssOut << c;
            continue;  // remove escape slash & pass along quote
        } else if (c == '"') {
            break;
        }
        ssOut << c;
    }

    std::string out = ssOut.str();
    ASSERT(out.length() > 0);
    return out;
}

int USDTProbeArg::getNextAsInt(const std::string& in) {
    std::stringstream ss(in);
    return getNextAsInt(ss);
}

int USDTProbeArg::getNextAsInt(std::stringstream& in) {
    std::string numStr;
    int num;

    in >> numStr;
    uassertStatusOK(
        mongo::NumberParser{}.allowTrailingText().skipWhitespace()(numStr.c_str(), &num));

    return num;
}

std::string USDTProbeArg::toJSONStr() {
    std::stringstream ss;
    ss << "{\"type\":\"";
    ss << (type == USDTProbeType::INT ? "int" : (type == USDTProbeType::STRING ? "str" : "struct"));
    ss << "\"";
    if (type == USDTProbeType::STRUCT) {
        ss << ", \"values\":[";
        bool first = true;
        for (auto arg : _members) {
            if (first) {
                first = false;
            } else {
                ss << ',';
            }
            ss << arg.toJSONStr();
        }
        ss << "]";
    } else if (type == USDTProbeType::STRING) {
        ss << ", \"length\":" << _length;
    }
    ss << "}";
    return ss.str();
}

std::string USDTProbe::toJSONStr() {
    std::stringstream ss;
    ss << "{\"name\":\"" << name << "\",";
    ss << "\"hits\":" << hits << ',';
    ss << "\"args\":[";
    bool first = true;
    for (unsigned short i = 0; i < _argc; i++) {
        if (first) {
            first = false;
        } else {
            ss << ',';
        }
        ss << _args[i].toJSONStr();
    }
    ss << "]}";
    return ss.str();
}

std::string toJSONStr(const std::vector<USDTProbe>& probes) {
    std::stringstream ss;
    ss << "{\"probes\":[";
    bool first = true;
    for (auto probe : probes) {
        if (first) {
            first = false;
        } else {
            ss << ',';
        }
        ss << probe.toJSONStr();
    }
    ss << "]}";
    return ss.str();
}

std::string readLine(int fdRd, int maxLen = 1024) {
    std::stringstream ss;

    int count = 0;
    char c;
    while (read(fdRd, &c, 1) && c != '\n' && count < maxLen) {
        ss << c;
        count++;
    }

    auto tmp = ss.str();
    return tmp;
}

std::string readUpTo(int fdRd, int len) {
    std::stringstream ss;
    int count = 0;
    while (count < len) {
        std::string s = readLine(fdRd);
        count += 1 + s.length();
        ss << s << "\n";
    }
    return ss.str();
}

USDTProbeTest::~USDTProbeTest() {
    setUp();
    size_t bytesWritten = write(_fdWr, "0\n", 2);
    ASSERT(bytesWritten == 2);
}

// handshake with python script
void USDTProbeTest::setUp() {
    char ack;
    size_t bytesRead = read(_fdRd, &ack, 1);
    ASSERT(bytesRead == 1 && ack == '>');
}

bool USDTProbeTest::runTest(const std::vector<USDTProbe>& probes,
                            const std::function<void()>& toTest) {
    setUp();

    // tell python what to expect
    std::stringstream ss;
    std::string json = toJSONStr(probes);
    ss << json.size() << std::endl;
    std::string sz = ss.str();

    size_t bytesWritten = write(_fdWr, sz.c_str(), sz.size());
    ASSERT(bytesWritten == sz.size());
    bytesWritten = write(_fdWr, json.c_str(), json.size());
    ASSERT(bytesWritten == json.size());

    // run actual test
    setUp();
    toTest();

    // retrieve test results
    std::string line;
    int size;

    size_t numPassed = 0;
    for (auto probe : probes) {
        line = readLine(_fdRd);
        ASSERT_EQ(line, probe.name);

        bool passed = false;
        for (int hit = 0; hit < probe.hits; hit++) {
            line = readLine(_fdRd);
            uassertStatusOK(
                mongo::NumberParser{}.allowTrailingText().skipWhitespace()(line.c_str(), &size));
            line = readUpTo(_fdRd, size);
            std::stringstream res(line);
            std::stringstream err;

            try {
                probe.onResult(res, hit);
                passed = true;
            } catch (const unittest::TestAssertionFailureException& e) {
                err << e.toString() << '\n';
            } catch (const mongo::DBException& db) {
                err << db.toString() << '\n';
            } catch (const std::exception& ex) {
                err << ex.what() << '\n';
            } catch (int x) {
                err << "caught int " << x << '\n';
            }

            if (passed) {
                std::cout << "PASSED [" << (hit + 1) << '/' << probe.hits << ']' << std::endl;
            } else {
                std::cout << "FAILED [" << (hit + 1) << '/' << probe.hits << ']' << std::endl;
                std::cout << err.str() << std::endl;
            }
        }
        if (passed) {
            numPassed++;
        }
    }

    return numPassed == probes.size();
}

}  // namespace mongo

// TODO test negative cases

int main(int argc, char** argv) {
    ASSERT_EQ(argc, 3);

    int fdRd, fdWr;
    uassertStatusOK(mongo::NumberParser{}(argv[1], &fdRd));
    uassertStatusOK(mongo::NumberParser{}(argv[2], &fdWr));

    mongo::USDTProbeTest tester(fdRd, fdWr);

    // dumb test
    std::vector<mongo::USDTProbe> dumbProbes;
    dumbProbes.push_back(mongo::USDTProbe("aProbe", 15, [](auto& res, int hit) -> void {}));
    ASSERT(tester.runTest(dumbProbes, []() -> void {
        for (int i = 0; i < 15; i++) {
            MONGO_USDT(aProbe);
        }
    }));

    // test INT args
    std::vector<mongo::USDTProbe> intProbes;
    intProbes.push_back(mongo::USDTProbe("probe1", 1, [](auto& res, int hit) -> void {
                            ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), 42);
                        }).withIntArg());

    intProbes.push_back(mongo::USDTProbe("probe2", 1, [](auto& res, int hit) -> void {
                            ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), 1);
                            ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), 2);
                        }).withIntArg(2));

    intProbes.push_back(mongo::USDTProbe("probe12", 1, [](auto& res, int hit) -> void {
                            for (int i = 12; i < 24; i++) {
                                ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), i);
                            }
                        }).withIntArg(12));

    intProbes.push_back(mongo::USDTProbe("probe1223", 23, [](auto& res, int hit) -> void {
                            for (int i = 12; i < 24; i++) {
                                ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), i + hit);
                            }
                        }).withIntArg(12));

    ASSERT(tester.runTest(intProbes, []() -> void {
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
    }));

    // ensure that passing along the wrong number is detected
    // for just one argument
    std::vector<mongo::USDTProbe> failures{
        mongo::USDTProbe("fails",
                         1,
                         [](auto& res, int hit) -> void {
                             ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), 42);
                         })
            .withIntArg()

    };
    ASSERT_FALSE(tester.runTest(failures, []() -> void { MONGO_USDT(fails, 4); }));

    // for many arguments
    std::vector<mongo::USDTProbe> failuresWithMany{
        mongo::USDTProbe("failsMany", 1, [](auto& res, int hit) -> void {
            ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), 42);
            ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), 43);
        }).withIntArg(2)};
    ASSERT_FALSE(tester.runTest(failuresWithMany, []() -> void { MONGO_USDT(failsMany, 42, 42); }));


    // test STRING args
    std::vector<mongo::USDTProbe> strProbes;
    strProbes.push_back(mongo::USDTProbe("probeA", 1, [](auto& res, int hit) -> void {
                            ASSERT_EQ(mongo::USDTProbeArg::getNextAsString(res), "albatross");
                        }).withStringArg(10));

    strProbes.push_back(
        mongo::USDTProbe("probeB",
                         1,
                         [](auto& res, int hit) -> void {
                             ASSERT_EQ("bard", mongo::USDTProbeArg::getNextAsString(res));
                             ASSERT_EQ("cantaLoupe!", mongo::USDTProbeArg::getNextAsString(res));
                         })
            .withStringArg(5)
            .withStringArg(12));

    mongo::USDTProbe probe12Str("probeC", 1, [](auto& res, int hit) -> void {
        for (int i = 0; i < 12; i++) {
            std::string actual =
                mongo::USDTProbeArg::getNextAsString(mongo::USDTProbeArg::getNextAsString(res));
            std::stringstream nexts;
            nexts << "str" << i;
            ASSERT_EQ(nexts.str(), actual);
        }
    });

    for (int i = 0; i < 12; i++) {
        probe12Str.withStringArg(i >= 10 ? 6 : 5);
    }

    strProbes.push_back(mongo::USDTProbe("probeComplex", 1, [](auto& res, int hit) -> void {
                            ASSERT_EQ("hello, World!\n \"salut, monde!\"",
                                      mongo::USDTProbeArg::getNextAsString(res));
                        }).withStringArg(34));

    ASSERT(tester.runTest(strProbes, []() -> void {
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
    }));

    // test STRUCT args
    std::vector<mongo::USDTProbe> structProbes;
    structProbes.push_back(
        mongo::USDTProbe("basicStruct",
                         1,
                         [](auto& res, int hit) -> void {
                             ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), 25);
                             ASSERT_EQ("hello", mongo::USDTProbeArg::getNextAsString(res));
                         })
            .withArg(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT)
                         .withIntMember()
                         .withStringMember(6)));

    structProbes.push_back(
        mongo::USDTProbe("nestedStruct",
                         1,
                         [](auto& res, int hit) -> void {
                             ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), 333);
                             ASSERT_EQ("duck", mongo::USDTProbeArg::getNextAsString(res));
                             ASSERT_EQ(mongo::USDTProbeArg::getNextAsInt(res), 22);
                         })
            .withArg(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT)
                         .withIntMember()
                         .withMember(mongo::USDTProbeArg(mongo::USDTProbeType::STRUCT)
                                         .withStringMember(5)
                                         .withIntMember())));

    ASSERT(tester.runTest(structProbes, []() -> void {
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
    }));

    return 0;
}
