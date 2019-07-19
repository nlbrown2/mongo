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

namespace mongo {

std::ostream& operator<<(std::ostream& out, const USDTProbeType& type) {
    switch (type) {
        case USDTProbeType::INT:
            out << "int";
            break;
        case USDTProbeType::STRING:
            out << "str";
            break;
        case USDTProbeType::STRUCT:
            out << "struct";
            break;
        case USDTProbeType::POINTER:
            out << "ptr";
            break;
    }
    return out;
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

int USDTProbeArg::getNextAsInt(std::stringstream& in) {
    std::string numStr;
    int num;

    in >> numStr;
    uassertStatusOK(mongo::NumberParser::strToAny()(numStr.c_str(), &num));

    return num;
}

void* USDTProbeArg::getNextAsPtr(std::stringstream& in) {
    void* res;
    ASSERT(in >> std::hex >> res);
    return res;
}

std::string USDTProbeArg::toJSONStr() const {
    std::stringstream ss;
    ss << "{\"type\":\"" << type << "\"";
    if (type == USDTProbeType::STRUCT) {
        ss << ", \"fields\":[";
        bool first = true;
        for (auto arg : _members) {
            if (first) {
                first = false;
            } else {
                ss << ',';
            }
            ss << arg;
        }
        ss << "]";
    } else if (type == USDTProbeType::STRING) {
        ss << ", \"length\":" << _length;
    }
    ss << "}";
    return ss.str();
}

std::ostream& operator<<(std::ostream& out, const USDTProbeArg& arg) {
    return out << arg.toJSONStr();
}

std::string USDTProbe::toJSONStr() const {
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
        ss << _args[i];
    }
    ss << "]}";
    return ss.str();
}

std::ostream& operator<<(std::ostream& out, const USDTProbe& probe) {
    return out << probe.toJSONStr();
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
        std::string s = readLine(fdRd, len);
        count += 1 + s.length();
        ss << s << "\n";
    }
    return ss.str();
}

void USDTProbeTest::_writeJSONToPipe(const std::string& json) {
    _setUpTest();

    std::string sz = std::to_string(json.size());

    size_t bytesWritten = write(_fdWr, sz.c_str(), sz.size());
    ASSERT(bytesWritten == sz.size());
    bytesWritten = write(_fdWr, "\n", 1);
    ASSERT(bytesWritten == 1);
    bytesWritten = write(_fdWr, json.c_str(), json.size());
    ASSERT(bytesWritten == json.size());
}

// provide python script with this process' pid
void USDTProbeTest::_initialize(char* fifoRd, char* fifoWr) {
    _fdWr = open(fifoWr, O_WRONLY);
    ASSERT(_fdWr > 0);
    _fdRd = open(fifoRd, O_RDONLY);
    if (_fdRd < 0)
        close(_fdWr);
    ASSERT(_fdRd > 0);

    std::stringstream ss;
    ss << "{\"pid\":" << getpid() << '}';
    _writeJSONToPipe(ss.str());
}

void USDTProbeTest::_destroy() {
    _setUpTest();
    ASSERT(write(_fdWr, "0", 1) == 1);
    close(_fdRd);
    close(_fdWr);
}

// handshake with python script prior to each test
void USDTProbeTest::_setUpTest() {
    char ack;
    size_t bytesRead = read(_fdRd, &ack, 1);
    ASSERT_EQ(bytesRead, static_cast<size_t>(1));
    ASSERT_EQ(ack, '>');
}

std::string USDTProbeTest::toJSONStr(const std::vector<USDTProbe>& probes) {
    std::stringstream ss;
    ss << "{\"probes\":[";
    bool first = true;
    for (auto probe : probes) {
        if (first) {
            first = false;
        } else {
            ss << ',';
        }
        ss << probe;
    }
    ss << "]}";
    return ss.str();
}

bool USDTProbeTest::runTest(const USDTProbe& probe, const std::function<void()>& toTest) {
    return runTest(std::vector<USDTProbe>{probe}, toTest);
}

bool USDTProbeTest::runTest(const std::vector<USDTProbe>& probes,
                            const std::function<void()>& toTest) {
    _writeJSONToPipe(toJSONStr(probes));

    // run test to trigger probes
    _setUpTest();
    toTest();

    // collect & verify test results
    std::string line;
    int size;
    size_t probes_passed = 0;
    for (auto probe : probes) {
        std::cout << "Testing [" << probe.name << "]" << std::endl;
        line = readLine(_fdRd);
        ASSERT_EQ(line, probe.name);

        int hits_passed = 0;
        for (int hit = 0; hit < probe.hits; hit++) {
            bool passed = false;
            line = readLine(_fdRd);
            uassertStatusOK(mongo::NumberParser::strToAny()(line.c_str(), &size));
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
                ++hits_passed;
            } else {
                std::cout << "FAILED [" << (hit + 1) << '/' << probe.hits << ']' << std::endl;
                std::cout << err.str() << std::endl;
            }
        }
        if (hits_passed == probe.hits) {
            probes_passed++;
        }
    }

    return probes_passed == probes.size();
}

}  // namespace mongo
