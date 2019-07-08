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

#include <functional>
#include <string>
#include <vector>

#include "mongo/unittest/unittest.h"

namespace mongo {

enum USDTProbeType {
    INT, STRING, STRUCT
};

class USDTProbeArg {
    std::vector<USDTProbeArg> _members;

public:
    USDTProbeType type; // TODO: can't be const

    USDTProbeArg() : _members(), type(USDTProbeType::INT) {}
    USDTProbeArg(USDTProbeType type) : _members(), type(type) {}
    
    USDTProbeArg& withMember(USDTProbeArg arg) {
        ASSERT_EQ(type, USDTProbeType::STRUCT);
        _members.push_back(arg);
        return *this; 
    }

    std::string toJSONStr();
};

class USDTProbe {
    USDTProbeArg _args[12]; 
    unsigned short _argc;

public:
    const int hits;
    const std::string name;
    const std::function<bool (const std::string&, int)> onResult;

    USDTProbe(const std::string name,
              int hits,
              const std::function<bool (const std::string&, int)> onResult)
        : _argc(0), hits(hits), name(name), onResult(onResult) {}

    USDTProbe& withArg(USDTProbeArg arg) {
        ASSERT_LT(_argc, 12);
        _args[_argc] = arg;
        _argc++;
        return *this;
    }

    USDTProbe& withIntArg(int num = 1) {
        for(int i=0; i<num; i++) {
            withArg(USDTProbeArg(USDTProbeType::INT));
        }
        return *this;
    }

    USDTProbe& withStringArg(int num = 1) {
        for(int i=0; i<num; i++) {
            withArg(USDTProbeArg(USDTProbeType::INT));
        }
        return *this;
    }

    std::string toJSONStr();
};

class USDTProbeTest {
    int _fdRd;
    int _fdWr;

    void setUp();

public:
    USDTProbeTest(int fdRd, int fdWr) : _fdRd(fdRd), _fdWr(fdWr) {}
    ~USDTProbeTest();

    void runTest(const std::vector<USDTProbe> &probes,
                 const std::function<void()> &toTest);
};

}  // namespace mongo
