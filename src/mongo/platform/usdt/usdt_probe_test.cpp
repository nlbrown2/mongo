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
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "mongo/base/parse_number.h"
#include "mongo/platform/usdt.h"
#include "mongo/util/assert_util.h"

namespace mongo {

std::string USDTProbeArg::toJSONStr() {
    std::stringstream ss;
    ss << "{\"type\":\"";
    ss << (type == USDTProbeType::INT ? "int" :
            (type == USDTProbeType::STRING ? "str" : "struct"));
    ss << "\"";
    if (type == USDTProbeType::STRUCT) {
        ss << ", \"values\":[";
        bool first = true;
        for (auto arg: _members) {
            if (first) {
                first = false;
            } else {
                ss << ',';
            }
            ss << arg.toJSONStr();
        }
        ss << "]";
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
    for(unsigned short i=0; i<_argc; i++) {
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

std::string toJSONStr(const std::vector<USDTProbe> &probes) {
    std::stringstream ss;
    ss << "{\"probes\":[";
    bool first = true;
    for(auto probe: probes) {
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
    while(read(fdRd, &c, 1) && c != '\n' && count < maxLen) {
        ss << c;
        count++;
    }

    auto tmp = ss.str();
    //std::cout << "******" << tmp << "******" << std::endl;
    return tmp;
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

void USDTProbeTest::runTest(const std::vector<USDTProbe> &probes,
                            const std::function<void()> &toTest) {
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

    // TODO: size should be produced before every probe
    line = readLine(_fdRd);
    uassertStatusOK(mongo::NumberParser{}
        .allowTrailingText()
        .skipWhitespace()(line.c_str(), &size));

    for(auto probe : probes) {
        line = readLine(_fdRd);
        ASSERT_EQ(line, probe.name);

        for(int hit = 0; hit < probe.hits; hit++) {
            line = readLine(_fdRd);
            if (probe.onResult(line, hit)) {
                std::cout << "PASSED [" << (hit+1) << '/' << probe.hits << ']' << std::endl;
            } else {
                std::cout << "FAILED [" << (hit+1) << '/' << probe.hits << ']' << std::endl;
            }
        }
    }
}

}  // namespace mongo

int main(int argc, char **argv) {
    ASSERT_EQ(argc, 3);

    int fdRd, fdWr;
    uassertStatusOK(mongo::NumberParser{}(argv[1], &fdRd));
    uassertStatusOK(mongo::NumberParser{}(argv[2], &fdWr));

    mongo::USDTProbeTest tester(fdRd, fdWr);

    // dumb test
    std::vector<mongo::USDTProbe> dumbProbes;
    dumbProbes.push_back(mongo::USDTProbe("aProbe", 15, [](const auto& res, int hit) -> bool {
        return true;
    }));
    tester.runTest(dumbProbes, []() -> void {
        for(int i=0; i<15; i++) {
            MONGO_USDT(aProbe);
        }
    });

    // test Int args
    std::vector<mongo::USDTProbe> intProbes;
    intProbes.push_back(mongo::USDTProbe("probe1", 1, [](const auto& res, int hit) -> bool {
        int val = -1;
        uassertStatusOK(mongo::NumberParser{}
            .allowTrailingText()
            .skipWhitespace()(res.c_str(), &val));
        return val == 42;
    }).withIntArg());

    intProbes.push_back(mongo::USDTProbe("probe2", 1, [](const auto& res, int hit) -> bool {
        int val1 = -1;
        int val2 = -2;

        mongo::NumberParser p;
        p.allowTrailingText()
         .skipWhitespace();
        
        char *s;
        uassertStatusOK(p(res.c_str(), &val1, &s));
        uassertStatusOK(p(s, &val2));

        return val1 == 1 && val2 == 2;
    }).withIntArg(2));

    intProbes.push_back(mongo::USDTProbe("probe12", 1, [](const auto& res, int hit) -> bool {
        int val = -1;
        mongo::NumberParser p;
        p.allowTrailingText()
         .skipWhitespace();

        char *s = const_cast<char *>(res.c_str());
        for(int i=12; i<24; i++) {
            uassertStatusOK(p(s, &val, &s));
            if (val != i) return false;
        }

        return true;
    }).withIntArg(12));

    intProbes.push_back(mongo::USDTProbe("probe1223", 23, [](const auto& res, int hit) -> bool {
        int val = -1;
        mongo::NumberParser p;
        p.allowTrailingText()
         .skipWhitespace();

        char *s = const_cast<char *>(res.c_str());
        for(int i=12; i<24; i++) {
            uassertStatusOK(p(s, &val, &s));
            if (val != i + hit) return false;
        }

        return true;
    }).withIntArg(12));

    tester.runTest(intProbes, []() -> void {
        MONGO_USDT(probe1, 42);
        MONGO_USDT(probe2, 1, 2);
        MONGO_USDT(probe12, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
        for(int i=0; i<23; i++) {
            MONGO_USDT(probe1223, i+12, i+13, i+14, i+15, i+16, i+17, i+18, i+19, i+20, i+21, i+22, i+23);
        }
    });

    // TODO: test String args
    #if 0
    std::vector<mongo::USDTProbe> strProbes;
    strProbes.push_back(mongo::USDTProbe("probe1", 1, [](const auto& res) -> bool {
        return val == "albatross";
    }).withIntArg());

    strProbes.push_back(mongo::USDTProbe("probe2", 1, [](const auto& res) -> bool {
        return val1 == 1 && val2 == 2;
    }).withIntArg(2));

    strProbes.push_back(mongo::USDTProbe("probe12", 1, [](const auto& res) -> bool {
        int val = -1;
        mongo::NumberParser p;
        p.allowTrailingText()
         .skipWhitespace();

        char *s = const_cast<char *>(res.c_str());
        for(int i=12; i<24; i++) {
            uassertStatusOK(p(s, &val, &s));
            if (val != i) return false;
        }

        return true;
    }).withIntArg(12));

    strProbes.push_back(mongo::USDTProbe("probe1223", 23, [](const auto& res) -> bool {
        int val = -1;
        mongo::NumberParser p;
        p.allowTrailingText()
         .skipWhitespace();

        char *s = const_cast<char *>(res.c_str());
        for(int i=12; i<24; i++) {
            uassertStatusOK(p(s, &val, &s));
            if (val != i) return false;
        }

        return true;
    }).withIntArg(12));

    tester.runTest(strProbes, []() -> void {
        MONGO_USDT(probe1, 42);
        MONGO_USDT(probe2, 1, 2);
        MONGO_USDT(probe12, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
        for(int i=0; i<23; i++) {
            MONGO_USDT(probe1223, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
        }
    });
    #endif

    return 0;
}
