/*
 * LSST Data Management System
 * Copyright 2008, 2009, 2010 LSST Corporation.
 *
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the LSST License Statement and
 * the GNU General Public License along with this program.  If not,
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */

// SqlInsertIter.h:
// class SqlInsertIter -- A class that finds INSERT statements in
// mysqldump output and iterates over them.
//

#ifndef LSST_QSERV_MERGER_SQLINSERTITER_H
#define LSST_QSERV_MERGER_SQLINSERTITER_H

// Third-party headers
#include <boost/regex.hpp>

// Local headers
#include "xrdc/PacketIter.h"

namespace lsst {
namespace qserv {
namespace merger {

class SqlInsertIter {
public:
    typedef char const* BufIter;
    typedef boost::regex_iterator<BufIter> Iter;
    typedef boost::match_results<BufIter> Match;
    typedef boost::sub_match<BufIter> Value;

    SqlInsertIter() {}

    /// Constructor.  Buffer must be valid over this object's lifetime.
    SqlInsertIter(char const* buf, off_t bufSize,
                  std::string const& tableName, bool allowNull);
    SqlInsertIter(xrdc::PacketIter::Ptr p,
                  std::string const& tableName, bool allowNull);

    // Destructor
    ~SqlInsertIter();

    // Dereference
    const Value& operator*() const { return (*_iter)[0]; }
    const Value* operator->() const { return &(*_iter)[0]; }

    // Increment
    SqlInsertIter& operator++();

    // Const accessors:
    bool operator==(SqlInsertIter const& rhs) const {
        return _iter == rhs._iter;
    }
    bool isDone() const;
    bool isMatch() const { return _blockFound; }
    bool isNullInsert() const;

private:
    SqlInsertIter operator++(int); // Disable: this isn't safe.

    void _init(char const* buf, off_t bufSize, std::string const& tableName);
    void _initRegex(std::string const& tableName);
    void _setupIter();
    void _increment();
    bool _incrementFragment();

    bool _allowNull;
    Iter _iter;
    Match _blockMatch;
    bool _blockFound;
    typedef unsigned long long BufOff;
    char* _pBuffer;
    BufOff _pBufSize;
    BufOff _pBufStart; // Start of non-junk in buffer
    BufOff _pBufEnd; // End of non-junk in buffer
    boost::regex _blockExpr;
    boost::regex _insExpr;
    boost::regex _nullExpr;
    xrdc::PacketIter::Ptr _pacIterP;

    static Iter _nullIter;
};

}}} // namespace lsst::qserv::merger

#endif // LSST_QSERV_MERGER_SQLINSERTITER_H
