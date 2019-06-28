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

//#include "usdt_probe_test.h"

#include <fcntl.h>
#include <iostream>
#include <sys/sdt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "mongo/base/parse_number.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"

#define CSTR_(str) const_cast<char *>(str.c_str())

// TODO: as class
void test(int fd_wr, const std::string &json) {
    size_t bytesWritten = write(fd_wr, CSTR_(json), json.size());
    ASSERT(bytesWritten == json.size());
    std::cout << "JSON written:" << std::endl << json << std::endl;
}

int main(int argc, char **argv) {
    ASSERT_EQ(argc, 3);

    int fd_rd, fd_wr;
    uassertStatusOK(mongo::NumberParser{}(argv[1], &fd_rd));
    uassertStatusOK(mongo::NumberParser{}(argv[2], &fd_wr));
    
    // handshake with python script
    char ack;
    size_t bytesRead = read(fd_rd, &ack, 1);
    ASSERT(bytesRead == 1 && ack == '>');

    std::cout << "Hand shook!" << std::endl; 

    std::string json = "{}";
    size_t bytesWritten = write(fd_wr, CSTR_(json), json.size());
    ASSERT(bytesWritten == json.size());

    test(fd_wr, "{}");
    test(fd_wr, "fun");

    // generate probes here
    return 0;
}
