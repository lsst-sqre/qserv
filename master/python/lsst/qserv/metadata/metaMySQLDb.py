#!/usr/bin/env python

# 
# LSST Data Management System
# Copyright 2008, 2009, 2010 LSST Corporation.
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

from __future__ import with_statement
import logging
import MySQLdb
import os
import subprocess
import sys

class MetaMySQLDb(object):
    """
    MetaMySQLDb class is a wrapper around MySQLdb for qserv metadata. 
    It contains a set of low level basic database utilities such 
    as connecting to database. It caches  connections, and handles 
    database errors.
    """
    def __init__(self):
        self._conn = None

    def __del__(self):
        self.disconnect()

    def isConnected(self):
        return self._conn != None

    def connect(self, createDb=False):
        """
        It connects to a server. If createDb flag is set, it will connect to
        the server, create the database, then connect to that database.
        """
        if self.isConnected():
            return
        config = lsst.qserv.master.config.config
        socket = config.get("metadb", "unix_socket")
        user = config.get("metadb", "user")
        passwd = config.get("metadb", "passwd")
        host = config.get("metadb", "host")
        port = config.getint("metadb", "port")
        self.dbName = config.get("metadb", "db")
        db4connect = None if createDb else self.dbName

        try: # Socket file first
            self._conn = sql.connect(user=user,
                                     passwd=passwd,
                                     unix_socket=socket,
                                     db=db4connect)
        except Exception, e:
            try:
                self._conn = sql.connect(user=user,
                                         passwd=passwd,
                                         host=host,
                                         port=port,
                                         db=db4connect)
            except Exception, e2:
                print >>sys.stderr, "Couldn't connect using file", socket, e
                msg = "Couldn't connect using %s:%s" % (host, port)
                print >> sys.stderr, msg, e2
                self._conn = None
                return
        c = self._conn.cursor()
        if createDb:
            self.dbExists(self, self.dbName, True) # throw exception on failure
            self.execCommand0("CREATE DATABASE %s" % self.dbName)
            self._conn.select_db(self.dbName)
        logging.debug("Connected to db %s" % self.dbName)

    def connectAndCreateDb(self):
        return connect(self, True):

    def disconnect(self):
        if self._conn == None:
            return
        try:
            self._conn.commit()
            self._conn.close()
        except MySQLdb.Error, e:
            raise RuntimeError("DB Error %d: %s" % (e.args[0], e.args[1]))
        logging.debug("MySQL connection closed")
        self._conn = None

    def dropDb(self):
        if self.dbExists(self.dbName):
            self.execCommand0("DROP DATABASE %s" % self.dbName)

    def checkDbExist(self, throwOnFailure=False):
        if self.dbName is None:
            raise RuntimeError("Invalid dbName")
        cmd = "SELECT COUNT(*) FROM information_schema.schemata "
        cmd += "WHERE schema_name = '%s'" % self.dbName
        count = self.execCommand1(cmd)
        if count[0] == 1:
                return True
        if throwOnFailure:
            raise RuntimeError("Database '%s' does not exist." % self.dbName)
        return False

    def createTable(self, tableName, tableSchema):
        self.execCommand0("CREATE TABLE %s %s" % (tableName, tableSchema))

    def checkTableExist(self, tableName, throwOnFailure=False):
        if ! isConnected():
            raise RuntimeError("Not connected")
        cmd = "SELECT COUNT(*) FROM information_schema.tables "
        cmd += "WHERE table_schema = '%s' AND table_name = '%s'" % \
               (self.dbName, tableName)
        count = self.execCommand1(cmd)
        if count[0] == 1:
            return True
        if throwOnFailure:
            raise RuntimeError("Table '%s' does not exist in db '%s'." % \
                               (tableName, self.dbName))
        return False

    def execCommand0(self, command):
        """
        Executes mysql commands which return no rows
        """
        return self._execCommand(command, 0)

    def execCommand1(self, command):
        """
        Executes mysql commands which return one rows
        """
        return self._execCommand(command, 1)

    def execCommandN(self, command):
        """
        Executes mysql commands which return multiple rows
        """
        return self._execCommand(command, 'n')

    def _execCommand(self, command, nRowsRet):
        """
        Executes mysql commands which return any number of rows.
        Expected number of returned rows should be given in nRowSet
        """
        if not self.isConnected():
            raise RuntimeError("No connection (command: '%s')" % command)

        cursor = self._conn.cursor()
        logging.debug("Executing %s" % command)
        cursor.execute(command)
        if nRowsRet == 0:
            ret = ""
        elif nRowsRet == 1:
            ret = cursor.fetchone()
            logging.debug("Got: %s" % str(ret))
        else:
            ret = cursor.fetchall()
            logging.debug("Got: %s" % str(ret))
        cursor.close()
        return ret
