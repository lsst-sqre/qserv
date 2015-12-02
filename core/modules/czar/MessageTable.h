/*
 * LSST Data Management System
 * Copyright 2015 AURA/LSST.
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
 * see <https://www.lsstcorp.org/LegalNotices/>.
 */
#ifndef LSST_QSERV_CZAR_MESSAGETABLE_H
#define LSST_QSERV_CZAR_MESSAGETABLE_H

// System headers
#include <string>
#include <memory>

// Third-party headers

// Qserv headers
#include "global/stringTypes.h"
#include "mysql/MySqlConfig.h"


namespace lsst {
namespace qserv {
namespace sql {
class SqlConnection;
}}}

namespace lsst {
namespace qserv {
namespace czar {

/// @addtogroup czar

/**
 *  @ingroup czar
 *
 *  @brief Class representing message table in results database.
 *
 */

class MessageTable  {
public:

    // Constructor takes table name including database name
    explicit MessageTable(std::string const& tableName, mysql::MySqlConfig const& resultConfig);

    /// Create and lock the table
    void lock();

    /// Update session ID
    void setSessionId(unsigned sessionId) { _sessionId = sessionId; }

    /// Release lock on message table so that proxy can proceed
    void unlock();

    /// Returns table name
    std::string const& tableName() const { return _tableName; }

protected:

private:

    /// store all messages from current session to the table
    void _saveQueryMessages();

    std::string const _tableName;
    int _sessionId = 0;
    std::shared_ptr<sql::SqlConnection> _sqlConn;

};

}}} // namespace lsst::qserv::czar

#endif // LSST_QSERV_CZAR_MESSAGETABLE_H
