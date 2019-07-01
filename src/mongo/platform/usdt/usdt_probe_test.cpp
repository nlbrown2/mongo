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
#include <sys/sdt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "mongo/base/parse_number.h"
#include "mongo/platform/usdt.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"

namespace mongo {

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

void USDTProbeTest::runTest(const std::string &json, const std::function<void()> &toTest) {
    setUp();

    // tell python what to expect
    std::stringstream ss;
    ss << json.size() << std::endl;
    std::string sz = ss.str();

    size_t bytesWritten = write(_fdWr, sz.c_str(), sz.size());
    ASSERT(bytesWritten == sz.size());
    bytesWritten = write(_fdWr, json.c_str(), json.size());
    ASSERT(bytesWritten == json.size());

    // run actual test 
    setUp();
    toTest();
}

}  // namespace mongo

int main(int argc, char **argv) {
    ASSERT_EQ(argc, 3);

    int fdRd, fdWr;
    uassertStatusOK(mongo::NumberParser{}(argv[1], &fdRd));
    uassertStatusOK(mongo::NumberParser{}(argv[2], &fdWr));

    mongo::USDTProbeTest tester(fdRd, fdWr);

    tester.runTest("{ \"probes\": [] }", []() -> void {
        std::cout << "No probes!" << std::endl;        
    });

    tester.runTest("{ \"probes\": [ {\"name\": \"probe1\", \"hits\": 1, \"args\": [] } ] }", []() -> void {
        MONGO_USDT(probe1);
    });

    tester.runTest("{ \"probes\": [ {\"name\": \"probe2\", \"hits\": 1, \"args\": [ { \"type\": \"int\", \"value\": 42} ] } ] }", []() -> void {
        MONGO_USDT(probe2, 42);
    });

    tester.runTest("{ \"probes\": [ {\"name\": \"probe2\", \"hits\": 1, \"args\": [ { \"type\": \"int\", \"value\": 42} ] } ] }", []() -> void {
        MONGO_USDT(probe2, 43);
    });

    return 0;
}
