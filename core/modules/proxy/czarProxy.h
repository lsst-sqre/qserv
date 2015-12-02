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
#ifndef LSST_QSERV_PROXY_CZARPROXY_H
#define LSST_QSERV_PROXY_CZARPROXY_H

// System headers
#include <map>
#include <string>
#include <vector>

// Third-party headers

// Qserv headers


namespace lsst {
namespace qserv {
namespace proxy {

/// @addtogroup proxy

/**
 *  @ingroup proxy
 *
 *  @brief C++ Interface between proxy and czar.
 *
 */

/**
 *  Returns list of strings:
 *  [0] error message, empty if all is fine
 *  [1] result table name
 *  [2] message table name
 *  [3] order by clause (optional)
 */
std::vector<std::string> submitQuery(std::string const& query,
                                     std::map<std::string, std::string> const& hints);

/**
 * Process a kill query command (experimental).
 *
 * @param query: (client)proxy-provided "KILL QUERY NNN" or "KILL NNN" string
 * @param clientId: client_dst_name from proxy
 * @return: Error message, empty on success
 */
std::string killQuery(std::string const& query, std::string const& clientId);

/**
 *  Send message to logging system. level is a string like "DEBUG".
 */
// NOTE: Do not change argument names, they are used by SWIG typemap
void log(std::string const& loggername, std::string const& level,
         std::string const& filename, std::string const& funcname,
         unsigned lineno, std::string const& message);

}}} // namespace lsst::qserv::proxy

#endif // LSST_QSERV_PROXY_CZARPROXY_H
