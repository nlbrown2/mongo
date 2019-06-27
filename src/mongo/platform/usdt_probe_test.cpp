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
#include <sys/sdt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// TODO: delete me
#include <iostream>

#include "mongo/base/parse_number.h"

namespace mongo {

// I HATE THIS
#define CSTR_(str) const_cast<char *>(str.c_str())

void USDTProbeTest::prepare(const std::string &fifoName, const std::string &json) {
    #if 0
    _fifoName = CSTR_(fifoName);

    int status = mkfifo(_fifoName, S_IRUSR | S_IWUSR);
    if (status != 0 && errno != EEXIST) {
        std::cerr << "mkfifo failed with errno: " << status << std::endl;
        exit(EXIT_FAILURE); // TODO not this
    } // else

    std::cout << "Fifo made: " << _fifoName << std::endl;
    int fd = open(_fifoName, O_RDWR);
    std::cout << "Forking..." << std::endl;
    #if 0
    int child = fork();

    if (child == 0) { // I am the child
        char* pyFile = CSTR_(kPythonFile);
        char* args[] = {pyFile, _fifoName, NULL};
        std::cout << "Child forked, running: " << kPythonFile << std::endl;
        execv(pyFile, args);

        std::cerr << "You should not be here :(" << errno << std::endl;
        exit(EXIT_FAILURE); // TODO not this
    } // I am the parent

    // now read one character from fifo, wait for my child to write
    char ack;
    fd = open(_fifoName, O_RDWR);
    int bytesRead = read(fd, &ack, 1);
    if (bytesRead != 1 || ack != '>') {
        std::cout << "Handshake failed." << std::endl;
    } else {
        std::cout << "Handshake succeeded!" << std::endl;
    }
    #endif
    close(fd);

    // write JSON through pipe to child
    fd = open(_fifoName, O_RDWR);
    int len = json.length() + 1;
    int bytesWritten = write(fd, json.c_str(), len);
    if (bytesWritten != len) {
        std::cerr << "You should not be here :(" << std::endl;
        exit(EXIT_FAILURE); // TODO not this
    }
    
    close(fd);
    #endif
}

void USDTProbeTest::tearDown() {
    // TODO signal death?
    #if 0
    int status;
    std::cout << "Parent awaiting child's exit." << std::endl;
    waitpid(-1, &status, 0);
    if (status != 0) {
        std::cout << "waitpid failed with errno: " << status << std::endl;
    }

    // clean up FIFO
    remove(_fifoName);
    #endif
}
}  // namespace mongo

int main(int argc, char **argv) {
    ASSERT_EQ(argc, 3);

    int fd_rd, fd_wr;
    mongo::NumberParser parse;
    Status s = NumberParser{}(argv[1], &fd_rd);
    if (!s.isOK()) throw(s);
    s = NumberParser{}(argv[2], &fd_wr);
    if (!s.isOK()) throw(s);
    

    return 0;
}
