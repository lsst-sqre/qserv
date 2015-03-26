#
# LSST Data Management System
# Copyright 2015 AURA/LSST.
#
# This product includes software developed by the
# LSST Project (http://www.lsst.org/).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the LSST License Statement and
# the GNU General Public License along with this program.  If not,
# see <http://www.lsstcorp.org/LegalNotices/>.
#

"""
Module defining Flask blueprint for xrootd management.

@author Andy Salnikov - salnikov@slac.stanford.edu
"""

#--------------------------------
#  Imports of standard modules --
#--------------------------------
import re
import logging
import os
import subprocess

#-----------------------------
# Imports for other modules --
#-----------------------------
from .config import Config
from .errors import errorResponse, ExceptionResponse
from lsst.db import db
from flask import Blueprint, json, request, url_for

#----------------------------------
# Local non-exported definitions --
#----------------------------------

_log = logging.getLogger('xrdMgr')

# pattern for valid db/table/column names
_idNameRe = re.compile(r'^[a-zA-Z_][0-9a-zA-Z_]*$')

def _validateId(idType, identifier):
    """
    Validate identifier, throws exception if identifier contains illegal characters
    """
    if not _idNameRe.match(identifier):
        raise ExceptionResponse(400, "InvalidArgument",
                                "{0} name is invalid: '{1}'".format(idType, identifier))

def _validateDbName(dbName):
    """ Validate database name """
    return _validateId("Database", dbName)

def _getArgFlag(mdict, option, default=True):
    """
    Extracts specified argument from request arguments and converts it to boolean flag.
    Argument is required to have value, possible values are '0', '1', 'yes', 'no', 'true', 'false'.
    """
    value = mdict.get(option, None)
    if value is None:
        return default
    value = value.strip().lower()
    if value not in ('0', '1', 'yes', 'no', 'true', 'false'):
        raise ExceptionResponse(400, "InvalidArgument",
                                "Unexpected value of '%s' option: \"%s\"" % (option, value))
    return value in ('1', 'yes', 'true')

def _dbDict(dbName):
    """ Make database instance dict out of db name """
    return dict(name=dbName, uri=url_for('.chunks', dbName=dbName))

def _runCmd(cmd, noexcept=True):
    """ Run command in a subprocess """
    try:
        _log.debug('executing command: %s', cmd)
        subprocess.check_call(cmd, close_fds=True)
        return 0
    except subprocess.CalledProcessError as exc:
        _log.debug('subprocess exception: %s', exc)
        if noexcept:
            return exc.returncode
        else:
            # re-throw
            raise

def _restartXrootd():
    """ Restart xrootd process, only if it is running already """
    initScript = os.path.join(Config.instance().runDir, 'etc/init.d/xrootd')

    cmd = [initScript, 'status']
    if _runCmd(cmd) == 0:
        try:
            cmd = [initScript, 'restart']
            _runCmd(cmd, False)
        except subprocess.CalledProcessError as exc:
            raise ExceptionResponse(409, "CommandFailure",
                                    "Failed to restart xrootd, please restart it manually", str(exc))

#------------------------
# Exported definitions --
#------------------------

xrdService = Blueprint('xrdService', __name__, template_folder='xrdService')


@xrdService.errorhandler(db.DbException)
def dbExceptionHandler(error):
    """ All leaked DbException exceptions make 500 error """
    return errorResponse(500, "db.DbException", str(error))

@xrdService.route('/dbs', methods=['GET'])
def dbs():
    """
    Return the list of databases known to xrootd.

    Currently we do not have interface for querying xrootd directly,
    instead we use the same information that ChunkInventory gets from
    database and we hope that xrootd is current w.r.t. that info.
    """

    _log.debug('request: %s', request)

    # use non-privileged account
    dbConn = Config.instance().dbConn()

    # TODO: Make sure that database name that we use here is correct.
    # The database name that holds Dbs table is constructed from the
    # xrootd instance name as qservw_<instance>. Currently we use fixed
    # instance name "worker" (defined in etc/init.d/xrootd)
    dbs = dbConn.execCommandN('SELECT db FROM qservw_worker.Dbs')

    dbs = [row[0] for row in dbs]
    _log.debug('dbs(): dbs = %s', dbs)

    dbData = []
    for dbName in dbs:
        dbData += [_dbDict(dbName)]

    return json.jsonify(results=dbData)


@xrdService.route('/dbs', methods=['POST'])
def dbAdd():
    """
    Add new database.

    Following parameters are expected to come in a request (in request body
    with application/x-www-form-urlencoded content like regular form):
        db: database name (required)
        xrootdRestart: if set to 'no' then do not restart xrootd
    """

    _log.debug('request: %s', request)
    _log.debug('request.form: %s', request.form)

    # get database name from query
    dbName = request.form.get('db', '').strip()
    if not dbName:
        raise ExceptionResponse(400, "MissingArgument", "Database name argument (db) is missing")

    # validate it
    _validateDbName(dbName)

    xrootdRestart = _getArgFlag(request.form, 'xrootdRestart', True)
    _log.debug('dbAdd(): xrootdRestart: %s', xrootdRestart)

    # apparently for now we need to use privileged account
    # as our regular account can only read from qservw_worker
    dbConn = Config.instance().privDbConn()

    # table is not indexed, to avoid multiple entries check that it's not defined yet
    try:
        query = "SELECT db FROM qservw_worker.Dbs WHERE db='{0}';".format(dbName)
        dbs = dbConn.execCommandN(query)
        if dbs:
            raise ExceptionResponse(409, "DatabaseExists", "Database %s is already defined" % dbName)
    except db.DbException as exc:
        _log.error('dbAdd(): exception when checking qservw_worker.Dbs: %s', exc)
        raise

    # now add it
    try:
        query = "INSERT INTO qservw_worker.Dbs (db) VALUES ('{0}')".format(dbName)
        dbs = dbConn.execCommand0(query)
    except db.DbException as exc:
        _log.error('dbAdd(): exception when adding database %s: %s', dbName, exc)
        raise

    _log.debug('dbAdd(): database %s added', dbName)

    # presently we have to restart xrootd to update ChunkInventory
    if xrootdRestart:
        _log.info('restarting xrootd after adding database %s', dbName)
        _restartXrootd()

    # return representation for new database
    return json.jsonify(result=_dbDict(dbName))


@xrdService.route('/dbs/<dbName>', methods=['DELETE'])
def dbRemove(dbName):
    """
    Remove database. Optional boolean xrootdRestart option can be given in
    a query string (acceptable values are "0', "1", "yes", "no"), if set
    to false it will disable xrootd restart.
    """

    _log.debug('request: %s', request)

    # validate it
    _validateDbName(dbName)

    xrootdRestart = _getArgFlag(request.args, 'xrootdRestart', True)
    _log.debug('dbAdd(): xrootdRestart: %s', xrootdRestart)

    # apparently for now we need to use privileged account
    # as our regular account can only read from qservw_worker
    dbConn = Config.instance().privDbConn()

    # TODO: replace with dbConn.cursor() when it's available
    dbConn.connect()
    cursor = dbConn._conn.cursor()
    try:
        query = "DELETE FROM qservw_worker.Dbs WHERE db=%s;"
        _log.debug('dbRemove(): executing: %s for %s', query, dbName)
        cursor.execute(query, [dbName])
        if cursor.rowcount == 0:
            _log.error('dbRemove(): no rows have been removed')
            raise ExceptionResponse(409, "DatabaseMissing", "Database %s is not defined" % dbName)
    except db.DbException as exc:
        _log.error('dbRemove(): exception when executing query: %s', exc)
        raise

    _log.debug('dbRemove(): database %s removed', dbName)

    if xrootdRestart:
        # presently we have to restart xrootd to update ChunkInventory
        _log.info('restarting xrootd after removing database %s', dbName)
        _restartXrootd()

    # return representation for new database
    return json.jsonify(result=_dbDict(dbName))


@xrdService.route('/dbs/<dbName>', methods=['GET'])
def chunks(dbName):
    """
    Return the list of chunk IDs in a database.

    Not implemented yet.
    """

    _log.debug('request: %s', request)

    # validate it
    _validateDbName(dbName)

    _log.error('chunks(): method not implemented yet')
    raise ExceptionResponse(501, "NotImplemented", "Chunk listing method is not implemented yet")