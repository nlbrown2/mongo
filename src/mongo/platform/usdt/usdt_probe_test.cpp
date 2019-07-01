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
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"

namespace mongo {

USDTProbeTest::~USDTProbeTest() {
    setUp();
    size_t bytesWritten = write(_fdWr, "0\n", 2);
    ASSERT(bytesWritten == 2);
    std::cout << "Hand unshook." << std::endl;
}

// handshake with python script
void USDTProbeTest::setUp() {
    char ack;
    size_t bytesRead = read(_fdRd, &ack, 1);
    ASSERT(bytesRead == 1 && ack == '>');
    std::cout << "Hand shook!" << std::endl;
}

void USDTProbeTest::runTest(const std::string &json, const std::function<void()> &toTest, const std::function<bool(const std::string&)>& onResult) {
    setUp();

    // tell python what to expect
    std::stringstream ss;
    ss << json.size() << std::endl;
    std::string sz = ss.str();

    size_t bytesWritten = write(_fdWr, sz.c_str(), sz.size());
    ASSERT(bytesWritten == sz.size());
    bytesWritten = write(_fdWr, json.c_str(), json.size());
    ASSERT(bytesWritten == json.size());
    
    std::cout << "JSON written:" << std::endl << json << std::endl; 

    // run actual test 
    setUp();
    toTest();
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
}

}  // namespace mongo

int main(int argc, char **argv) {
    ASSERT_EQ(argc, 3);

    int fdRd, fdWr;
    uassertStatusOK(mongo::NumberParser{}(argv[1], &fdRd));
    uassertStatusOK(mongo::NumberParser{}(argv[2], &fdWr));

    mongo::USDTProbeTest tester(fdRd, fdWr);

    tester.runTest(R"(
                {"probes": [{
                    "name": "new_json",
                    "hits": 1,
                    "args": [
                        {   "type": "struct",
                            "values": [
                        {
                            "type": "int"
                        },{
                            "type": "str",
                            "length": 20
                        },{
                            "type": "struct",
                            "values": [
                                {
                                    "type": "int"
                                },{
                                    "type": "struct",
                                    "values": [
                                        {
                                            "type": "int"
                                        }
                                    ]
                                }
                            ] 
                        }]}
                    ] 
                }]})", []() -> void {
            struct {
                int val = 42;
                char str[20] = "Hello, world!";
                struct {
                    int num = 43;
                    struct{
                        int hidden = 44;
                    } double_nest;
                } nested;
            } Struct;
            std::cout << "pre probe" << std::endl;
            MONGO_USDT(new_json, &Struct);
            std::cout << "post probe" << std::endl;
    }, [](const std::string& from_pipe) {
        std::cout << "Got: " << from_pipe << std::endl;
        return false;
    });
 
    /* tester.runTest("{ \"probes\": [] }", []() -> void { */
    /*     std::cout << "No probes!" << std::endl; */        
    /* }); */

    /* tester.runTest("{ \"probes\": [ {\"name\": \"probe1\", \"hits\": 1, \"args\": [] } ] }", []() -> void { */
    /*     MONGO_USDT(probe1); */
    /* }); */

    /* tester.runTest("{ \"probes\": [ {\"name\": \"probe2\", \"hits\": 1, \"args\": [ { \"type\": \"int\", \"value\": 42} ] } ] }", []() -> void { */
    /*     MONGO_USDT(probe2, 42); */
    /* }); */

    return 0;
}
