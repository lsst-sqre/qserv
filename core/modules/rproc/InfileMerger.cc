// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2014 LSST Corporation.
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
/**
  * @file
  *
  * @brief InfileMerger implementation
  *
  * InfileMerger is a class that is responsible for the organized
  * merging of query results into a single table that can be returned
  * to the user. The current strategy loads dumped chunk result tables
  * from workers into a single table, followed by a
  * merging/aggregation query (as needed) to produce the final user
  * result table.
  *
  * @author Daniel L. Wang, SLAC
  */

#include "rproc/InfileMerger.h"

// System headers
#include <iostream>
#include <sstream>
#include <sys/time.h>

// Third-party headers
#include <boost/format.hpp>
#include <boost/regex.hpp>

// Local headers
#include "log/Logger.h"
#include "mysql/LocalInfile.h"
#include "mysql/MySqlConnection.h"
#include "proto/worker.pb.h"
#include "proto/ProtoImporter.h"
#include "rproc/ProtoRowBuffer.h"
#include "rproc/SqlInsertIter.h"
#include "sql/Schema.h"
#include "sql/SqlConnection.h"
#include "sql/SqlResults.h"
#include "sql/SqlErrorObject.h"
#include "sql/statement.h"
#include "util/MmapFile.h"
#include "util/StringHash.h"


namespace { // File-scope helpers

using lsst::qserv::mysql::MySqlConfig;
using lsst::qserv::proto::ProtoHeader;
using lsst::qserv::proto::ProtoImporter;
using lsst::qserv::rproc::InfileMergerConfig;

std::string getTimeStampId() {
    struct timeval now;
    int rc = gettimeofday(&now, NULL);
    if (rc != 0) throw "Failed to get timestamp.";
    std::stringstream s;
    s << (now.tv_sec % 10000) << now.tv_usec;
    return s.str();
    // Use the lower digits as pseudo-unique (usec, sec % 10000)
    // FIXME: is there a better idea?
}

boost::shared_ptr<MySqlConfig> makeSqlConfig(InfileMergerConfig const& c) {
    boost::shared_ptr<MySqlConfig> sc(new MySqlConfig());
    assert(sc.get());
    sc->username = c.user;
    sc->dbName = c.targetDb;
    sc->socket = c.socket;
    return sc;
}


} // anonymous namespace



namespace lsst {
namespace qserv {
namespace rproc {

std::string const InfileMerger::_dropSql("DROP TABLE IF EXISTS %s;");
std::string const InfileMerger::_createSql("CREATE TABLE IF NOT EXISTS %s SELECT * FROM %s;");
std::string const InfileMerger::_createFixSql("CREATE TABLE IF NOT EXISTS %s SELECT %s FROM %s %s;");
std::string const InfileMerger::_insertSql("INSERT INTO %s SELECT * FROM %s;");
std::string const InfileMerger::_cleanupSql("DROP TABLE IF EXISTS %s;");
std::string const InfileMerger::_cmdBase("%1% --socket=%2% -u %3% %4%");
////////////////////////////////////////////////////////////////////////
// InfileMergerError
////////////////////////////////////////////////////////////////////////
bool InfileMergerError::resultTooBig() const {
    return (status == MYSQLEXEC) && (errorCode == 1114);
}

////////////////////////////////////////////////////////////////////////
// InfileMerger::Msgs
////////////////////////////////////////////////////////////////////////
class InfileMerger::Msgs {
public:
    lsst::qserv::proto::ProtoHeader protoHeader;
    lsst::qserv::proto::Result result;
};

////////////////////////////////////////////////////////////////////////
// InfileMerger::Mgr
////////////////////////////////////////////////////////////////////////
class InfileMerger::Mgr {
public:
    class Action;
    friend class Action;

    Mgr(MySqlConfig const& config);

    /// Takes ownership of msgs.
    Action newAction(std::string const& table, std::auto_ptr<Msgs> msgs);
    bool applyMysql(std::string const& query);

    void signalDone() {
        boost::lock_guard<boost::mutex> lock(_inflightMutex);
        --_numInflight;
    }

private:
    inline void _incrementInflight() {
        boost::lock_guard<boost::mutex> lock(_inflightMutex);
        ++_numInflight;
    }

    mysql::MySqlConnection _mysqlConn;
    lsst::qserv::mysql::LocalInfile::Mgr _infileMgr;
    boost::mutex _inflightMutex;
    boost::mutex _mysqlMutex;
    int _numInflight;
};

class InfileMerger::Mgr::Action {
public:
    Action(Mgr& mgr,
           std::auto_ptr<Msgs> msgs,
           std::string const& table, std::string const& virtFile)
        : _mgr(mgr), _msgs(msgs), _table(table), _virtFile(virtFile) {
    }
    Action(Mgr& mgr, std::auto_ptr<Msgs> msgs, std::string const& table)
        : _mgr(mgr), _msgs(msgs), _table(table) {
        _virtFile = mgr._infileMgr.prepareSrc(rproc::newProtoRowBuffer(msgs->result));
        mgr._incrementInflight();
    }
    void operator()() {
        // load data infile.
        std::string infileStatement = sql::formLoadInfile(_table, _virtFile);
        bool result = _mgr.applyMysql(infileStatement);
        assert(result);
        // TODO: do something with the result so we can catch errors.
    }
    Mgr& _mgr;
    std::auto_ptr<Msgs> _msgs;
    std::string _table;
    std::string _virtFile;
};

////////////////////////////////////////////////////////////////////////
// InfileMerger::Mgr implementation
////////////////////////////////////////////////////////////////////////
InfileMerger::Mgr::Mgr(MySqlConfig const& config)
    : _mysqlConn(config, true),
      _numInflight(0) {
    if(_mysqlConn.connect()) {
        _infileMgr.attach(_mysqlConn.getMySql());
    } else {
        throw InfileMergerError(InfileMergerError::MYSQLCONNECT);
    }
}

bool InfileMerger::Mgr::applyMysql(std::string const& query) {
    boost::lock_guard<boost::mutex> lock(_mysqlMutex);
    if(!_mysqlConn.connected()) {
        return false; // should have connected during Mgr construction
        // Maybe we should reconnect?
    }
    bool result = _mysqlConn.queryUnbuffered(query);
    if(result) {
        _mysqlConn.getResult(); // ignore result for now
        _mysqlConn.freeResult();
    }
    return result;
}

////////////////////////////////////////////////////////////////////////
// InfileMerger public
////////////////////////////////////////////////////////////////////////
InfileMerger::InfileMerger(InfileMergerConfig const& c)
    : _config(c),
      _sqlConfig(makeSqlConfig(c)),
      _tableCount(0),
      _isFinished(false),
      _msgs(new Msgs),
      _mgr(new Mgr(*_sqlConfig)),
      _needCreateTable(true) {
    _error.errorCode = InfileMergerError::NONE;

    _fixupTargetName();
#if 0
    _loadCmd = (boost::format(_cmdBase)
		% c.mySqlCmd % c.socket % c.user % c.targetDb).str();
#endif
}

int InfileMerger::_fetchHeader(char const* buffer, int length) {
    // First char: sizeof protoheader. always less than 255 char.
    unsigned char phSize = *reinterpret_cast<unsigned char const*>(buffer);
    // Advance cursor to point after length
    char const* cursor = buffer + 1;
    int remain = length - 1;

    if(!ProtoImporter<ProtoHeader>::setMsgFrom(_msgs->protoHeader,
                                               cursor, phSize)) {
        _error.errorCode = InfileMergerError::HEADER_IMPORT;
        _error.description = "Error decoding proto header";
        // This is only a real error if there are no more bytes.
        return 0;
    }
    cursor += phSize; // Advance to Result msg
    remain -= phSize;
    if(remain < _msgs->protoHeader.size()) {
        _error.description = "Buffer too small for result msg, increase buffer size in InfileMerger";
        _error.errorCode = InfileMergerError::HEADER_OVERFLOW;
        return 0;
    }
    // Now decode Result msg
    int resultSize = _msgs->protoHeader.size();
    if(!ProtoImporter<proto::Result>::setMsgFrom(_msgs->result,
                                                 cursor, resultSize)) {
        _error.errorCode = InfileMergerError::RESULT_IMPORT;
        _error.description = "Error decoding result msg";
        throw _error;
    }

    //_msgs->result.PrintDebugString(); // wait a second.
    // doublecheck session
    _msgs->result.session(); // TODO
    _setupTable();
    if(_error.errorCode) {
        return -1;
    }

    std::string computedMd5 = util::StringHash::getMd5(cursor, resultSize);
    if(_msgs->protoHeader.md5() != computedMd5) {
        _error.description = "Result message MD5 mismatch";
        _error.errorCode = InfileMergerError::RESULT_MD5;
        return 0;
    }
    _needHeader = false;
    // Setup infile properties
    // Spawn thread to handle infile processing
    Mgr::Action a(*_mgr, _msgs, _mergeTable);
    boost::thread *t = new boost::thread(a);
    t->join();
    delete t;
    // FIXME: if we spawn a separate thread, we need to save it if we're going to continue before it completes.

    assert(!_msgs.get()); // Ownership should have transferred.

//    _thread = new boost::thread(_mgr.newAction(_mgr, _msgs, _mergeTable));

    return 0;
}
int InfileMerger::_waitPacket(char const* buffer, int length) {
    // process buffer into rows, as much as possible, saving leftover.
    // consume buffer into rows, and flag return for file handler.

    return 0; // FIXME

}

void InfileMerger::_setupTable() {
    // Create table, using schema
    boost::lock_guard<boost::mutex> lock(_createTableMutex);

    if(_needCreateTable) {
        // create schema
        proto::RowSchema const& rs = _msgs->result.rowschema();
        sql::Schema s;
        for(int i=0, e=rs.columnschema_size(); i != e; ++i) {
            proto::ColumnSchema const& cs = rs.columnschema(i);
            sql::ColSchema scs;
            scs.name = cs.name();
            if(cs.hasdefault()) {
                scs.defaultValue = cs.defaultvalue();
                scs.hasDefault = true;
            } else {
                scs.hasDefault = false;
            }
            if(cs.has_mysqltype()) {
                scs.colType.mysqlType = cs.mysqltype();
            }
            scs.colType.sqlType = cs.sqltype();

            s.columns.push_back(scs);
        }
        std::string createStmt = sql::formCreateTable(_mergeTable, s);
        LOGGER_DBG << "InfileMerger create table:" << createStmt << std::endl;

        if(!_applySqlLocal(createStmt)) {
            _error.errorCode = InfileMergerError::CREATE_TABLE;
            _error.description = "Error creating table (" + _mergeTable + ")";
            _isFinished = true; // Cannot continue.
            return;
        }
        _needCreateTable = false;
    } else {
        // Do nothing, table already created.
    }
}
void InfileMerger::_setupRow() {
    /// Setup infile to merge.
    // ???
}
off_t InfileMerger::merge(char const* dumpBuffer, int dumpLength,
                         std::string const& tableName) {
    if(_error.errorCode) { // Do not attempt when in an error state.
        return -1;
    }
    LOGGER_DBG << "EXECUTING InfileMerger::merge(packetbuffer, " << tableName << ")" << std::endl;
    char const* buffer;
    int length;
    // Now buffer is big enough, start processing.
    if(_needHeader) {
        int hSize = _fetchHeader(dumpBuffer, dumpLength);
        if(hSize == 0) {
            // Buffer not big enough.
            return 0;
        }
        buffer = dumpBuffer + hSize;
        length = dumpLength - hSize;
        _waitPacket(buffer, length);
    } else {
        _waitPacket(dumpBuffer, dumpLength);
    }
    return dumpLength; // _waitPacket doesn't return until it consumes the whole buffer.
#if 0
    bool allowNull = true;
    CreateStmt cs(dumpBuffer, dumpLength, tableName,
                  _config.targetDb, _mergeTable);
    _createTableIfNotExists(cs);
    SqlInsertIter sii(dumpBuffer, dumpLength, tableName, allowNull);
    bool successful = _importIter(sii, tableName);
    if(!successful) {
        // FIXME
    }
    off_t used = sii.getLastUsed() - dumpBuffer;
    return used;
#endif
}
bool InfileMerger::finalize() {
#if 0
   if(_isFinished) {
        LOGGER_ERR << "InfileMerger::finalize(), but _isFinished == true"
                   << std::endl;
    }
    if(_mergeTable != _config.targetTable) {
        std::string cleanup = (boost::format(_cleanupSql) % _mergeTable).str();
        std::string fixupSuffix = _config.mFixup.post + _buildOrderByLimit();

        // Need to perform fixup for aggregation.
        std::string sql = (boost::format(_createFixSql)
                           % _config.targetTable
                           % _config.mFixup.select
                           % _mergeTable
                           % fixupSuffix).str() + cleanup;
        LOGGER_INF << "Merging w/" << sql << std::endl;
        return _applySql(sql);
    }
    LOGGER_INF << "Merged " << _mergeTable << " into " << _config.targetTable
               << std::endl;
    _isFinished = true;
    return true;
#else
    return false;
#endif
}

bool InfileMerger::isFinished() const {
    return _isFinished;
}

bool InfileMerger::_applySqlLocal(std::string const& sql) {
    boost::lock_guard<boost::mutex> m(_sqlMutex);
    sql::SqlErrorObject errObj;
    if(!_sqlConn.get()) {
        _sqlConn.reset(new sql::SqlConnection(*_sqlConfig, true));
        if(!_sqlConn->connectToDb(errObj)) {
            _error.status = InfileMergerError::MYSQLCONNECT;
            _error.errorCode = errObj.errNo();
            _error.description = "Error connecting to db. " + errObj.printErrMsg();
            _sqlConn.reset();
            return false;
        } else {
            LOGGER_INF << "TableMerger " << (void*) this
                       << " connected to db." << std::endl;
        }
    }
    if(!_sqlConn->runQuery(sql, errObj)) {
        _error.status = InfileMergerError::MYSQLEXEC;
        _error.errorCode = errObj.errNo();
        _error.description = "Error applying sql. " + errObj.printErrMsg();
        return false;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////
// private
////////////////////////////////////////////////////////////////////////
void InfileMerger::_fixupTargetName() {
    if(_config.targetTable.empty()) {
        assert(!_config.targetDb.empty());
        _config.targetTable = (boost::format("%1%.result_%2%")
                               % _config.targetDb % getTimeStampId()).str();
    }

    if(_config.mFixup.needsFixup) {
        // Set merging temporary if needed.
        _mergeTable = _config.targetTable + "_m";
    } else {
        _mergeTable = _config.targetTable;
    }
}
#if 0
bool TableMerger::_applySql(std::string const& sql) {
    return _applySqlLocal(sql); //try local impl now.
    FILE* fp;
    {
        boost::lock_guard<boost::mutex> m(_popenMutex);
        fp = popen(_loadCmd.c_str(), "w"); // check error
    }
    if(fp == NULL) {
        _error.status = TableMergerError::MYSQLOPEN;
        _error.errorCode = 0;
        _error.description = "Error starting mysql process.";
        return false;
    }
    int written = fwrite(sql.c_str(), sizeof(std::string::value_type),
                         sql.size(), fp);
    if(((unsigned)written) != (sql.size() * sizeof(std::string::value_type))) {
        _error.status = TableMergerError::MERGEWRITE;
        _error.errorCode = written;
        _error.description = "Error writing sql to mysql process.." + sql;
        {
            boost::lock_guard<boost::mutex> m(_popenMutex);
            pclose(fp); // cleanup
        }
        return false;
    }
    int r;
    {
        boost::lock_guard<boost::mutex> m(_popenMutex);
        r = pclose(fp);
    }
    if(r == -1) {
        _error.status = TableMergerError::TERMINATE;
        _error.errorCode = r;
        _error.description = "Error finalizing merge step..";
        return false;
    }
    return true;
}


std::string TableMerger::_buildMergeSql(std::string const& tableName,
                                        bool create) {
    std::string cleanup = (boost::format(_cleanupSql) % tableName).str();

    if(create) {
        return (boost::format(_dropSql) % _mergeTable).str()
            + (boost::format(_createSql) % _mergeTable
               % tableName).str() + cleanup;
    } else {
        return (boost::format(_insertSql) %  _mergeTable
                % tableName).str() + cleanup;
    }
}

std::string TableMerger::_buildOrderByLimit() {
    std::stringstream ss;
    if(!_config.mFixup.orderBy.empty()) {
        ss << "ORDER BY " << _config.mFixup.orderBy;
    }
    if(_config.mFixup.limit != -1) {
        if(!_config.mFixup.orderBy.empty()) { ss << " "; }
        ss << "LIMIT " << _config.mFixup.limit;
    }
    return ss.str();
}

bool TableMerger::_createTableIfNotExists(TableMerger::CreateStmt& cs) {
    LOGGER_DBG << "Importing " << cs.getTable() << std::endl;
    boost::lock_guard<boost::mutex> g(_countMutex);
    ++_tableCount;
    if(_tableCount == 1) {
        bool isOk = _dropAndCreate(cs.getTable(), cs.getStmt());
        if(!isOk) {
            --_tableCount; // We failed merging the table.
            return false;
        }
    }
}

bool TableMerger::_importResult(std::string const& dumpFile) {
    int rc = system((_loadCmd + "<" + dumpFile).c_str());
    if(rc != 0) {
        _error.status = TableMergerError::IMPORT;
        _error.errorCode = rc;
        _error.description = "Error importing result db.";
        return false;
    }
    return true;
}

bool TableMerger::merge2(std::string const& dumpFile,
                        std::string const& tableName) {
    boost::shared_ptr<util::MmapFile> m =
        util::MmapFile::newMap(dumpFile, true, false);
    if(!m.get()) {
        // Fallback to non-mmap version.
        return _slowImport(dumpFile, tableName);
    }
    char const* buf = static_cast<char const*>(m->getBuf());
    ::off_t size = m->getSize();
    bool allowNull = false;
    CreateStmt cs(buf, size, tableName, _config.targetDb, _mergeTable);
    _createTableIfNotExists(cs);
    // No locking needed if not first, after updating the counter.
    // Once the table is created, everyone should insert.
    return _importBufferInsert(buf, size, tableName, allowNull);
}

bool TableMerger::_dropAndCreate(std::string const& tableName,
                                 std::string createSql) {

    std::string dropSql = "DROP TABLE IF EXISTS " + tableName + ";";
    if(_config.dropMem) {
        std::string const memSpec = "ENGINE=MEMORY";
        std::string::size_type pos = createSql.find(memSpec);
        if(pos != std::string::npos) {
            createSql.erase(pos, memSpec.size());
        }
    }
    LOGGER_DBG << "CREATE-----" << _mergeTable << std::endl;
    return _applySql(dropSql + createSql);
}

bool TableMerger::_importIter(SqlInsertIter& sii,
                               std::string const& tableName) {
    LOGGER_DBG << "EXECUTING TableMerger::_importIter(sii, " << tableName << ")" << std::endl;
    int insertsCompleted = 0;
    // Search the buffer for the insert statement,
    // patch it (and future occurrences for the old table name,
    // and merge directly.
    LOGGER_DBG << "MERGE INTO-----" << _mergeTable << std::endl;
    for(; !sii.isDone(); ++sii) {
        char const* stmtBegin = sii->first;
        std::size_t stmtSize = sii->second - stmtBegin;
        std::string q(stmtBegin, stmtSize);
        bool dropQuote = (std::string::npos != _mergeTable.find("."));
        inplaceReplace(q, tableName,
                       dropDbContext(_mergeTable, _config.targetDb),
                       dropQuote);
        if(!_applySql(q)) {
            if(_error.resultTooBig()) {
                std::stringstream errStrm;
                errStrm << "Failed importing! " << tableName << " " << _error.description;
                LOGGER_ERR << errStrm.str() << std::endl;
                throw errStrm.str();
            }
            return false;
        }
        ++insertsCompleted;
    }
    return true;
}

bool TableMerger::_importBufferInsert(char const* buf, std::size_t size,
                                      std::string const& tableName,
                                      bool allowNull) {
    SqlInsertIter sii(buf, size, tableName, allowNull);
    bool successful = _importIter(sii, tableName);
    if(!successful) {
        LOGGER_ERR << "Error importing to " << tableName
                   << " buffer of size=" << size << std::endl;
        return false;
    }
    return true;
}

bool TableMerger::_slowImport(std::string const& dumpFile,
                              std::string const& tableName) {
    assert(false);
    bool isOk = true;
    std::string sql;
    _importResult(dumpFile);
    {
        LOGGER_DBG << "Importing " << tableName << std::endl;
        boost::lock_guard<boost::mutex> g(_countMutex);
        ++_tableCount;
        if(_tableCount == 1) {
            sql = _buildMergeSql(tableName, true);
            isOk = _applySql(sql);
            if(!isOk) {
                LOGGER_ERR << "Failed importing! " << tableName
                           << " " << _error.description << std::endl;
                --_tableCount; // We failed merging the table.
            }
	    return isOk; // must happen first.
        }
    }
    // No locking needed if not first, after updating the counter.
    sql = _buildMergeSql(tableName, false);
    return _applySql(sql);
}
#endif
}}} // namespace lsst::qserv::rproc
