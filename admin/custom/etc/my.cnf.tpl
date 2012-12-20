[mysqld]

datadir=<MYSQLD_DATA_DIR>
socket=<QSERV_BASE_DIR>/var/lib/mysql/mysql.sock
# port=3306
port=<MYSQLD_PORT>

# Disabling symbolic-links is recommended to prevent assorted security risks
symbolic-links=0

#
# * Logging and Replication
#
# Both location gets rotated by the cronjob.
# Be aware that this log type is a performance killer.
# general-log=<QSERV_LOG_DIR>/mysql-queries.log

[mysqld_safe]

log-error=<QSERV_LOG_DIR>/mysqld.log
pid-file=<QSERV_BASE_DIR>/var/run/mysqld/mysqld.pid

