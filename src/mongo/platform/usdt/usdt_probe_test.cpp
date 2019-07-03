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
    ss << "\"hits\":" << _hits << ',';
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
        std::cout << "**" << c << std::endl;
        ss << c;
        count++;
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

// ASSUMPTION: TODO @Nathan? does results guarantee probe order?
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
    int size = 1024; // TODO verify this/ get from py
    for(auto probe : probes) {
        std::cout << "READ RES OF " << probe.name << std::endl;
        line = readLine(_fdRd);
        ASSERT_EQ(line, probe.name);
       
        //line = readLine(_fdRd);
        //uassertStatusOK(mongo::NumberParser{}(line, &size));
        line = readLine(_fdRd, size); // get results
        std::cout << line << "***" << std::endl;

        if(probe.onResult(line)) {
            std::cout << "PASSED" << std::endl;
        } else {
            std::cout << "FAILED" << std::endl;
        }
    }

    #if 0
    std::cout << "reading size?" << std::endl;
    char buffer[1024];
    std::string number;
    for(bool seenNewline = false; !seenNewline;) {
        int bytesRead = read(_fdRd, buffer, sizeof(buffer));
        for(int i = 0; i < bytesRead; ++i) {
            if(buffer[i] == '\n')
                seenNewline = true;
        }
        number.append(buffer, bytesRead);
    }
    long long size;
    ASSERT_OK(NumberParser::strToAny()(number.c_str(), &size));
    std::cout << "size: " << size << std::endl;
    std::string values;
    for(int bytesRead = 0; size; size -= bytesRead) {
        bytesRead = read(_fdRd, &buffer, sizeof(buffer));
        values.append(buffer, bytesRead);
    }
    if(onResult(values)) {
        std::cout << "PASSED" << std::endl;
    } else {
        std::cout << "FAILED" << std::endl;
    }
    #endif
}

}  // namespace mongo

int main(int argc, char **argv) {
    ASSERT_EQ(argc, 3);

    int fdRd, fdWr;
    uassertStatusOK(mongo::NumberParser{}(argv[1], &fdRd));
    uassertStatusOK(mongo::NumberParser{}(argv[2], &fdWr));

    mongo::USDTProbeTest tester(fdRd, fdWr);

    std::vector<mongo::USDTProbe> probes;
    probes.push_back(mongo::USDTProbe("probe", 1, [](const auto& res) -> bool {
        std::cout << "ONRESULT" << std::endl;
        int val = -1;
        uassertStatusOK(mongo::NumberParser{}(res.c_str(), &val));
        return val == 42;
    }).withArg(mongo::USDTProbeArg(mongo::USDTProbeType::INT)));

    tester.runTest(probes, []() -> void {
        std::cout << "ONTEST" << std::endl;
        MONGO_USDT(probe, 42);
    });

    return 0;
}
