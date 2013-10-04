### common config ###

# node_id
#	yeti instance id
#	default: mandatory
#
# pop_id
#	yeti instance point of presence id
#	default: mandatory

node_id = 1
pop_id = 1

# db_schema
#	schema name which used for getprofile query and
#	for sqlrouter,codes translator & resources control configs loading
#	default: mandatory

db_schema = switch

### SqlRouter config ###

# database connection settings suffixes. similar for sqlrouter and cdrwriter 
# host
#	database host
#	default: 127.0.0.1
#
# port
#	database port
#	default: 5432
#
# user
#	database username
#	default: sqlrouter
#
# name
#	database name
#	default: sqlrouter
#
# pass
#	database password
#	default: sqlrouter

master_host=127.0.0.1
master_port=5432
master_user=routeserver
master_name=routeserver
master_pass=routeserver

# pool_size
#	how many connections will be opened for each database 	
#	default: 10
#
# check_interval
#	interval between unused connections check in seconds
#	default: 25
#
# max_wait
#	timeout of connection obtaining from pool in milliseconds
#	this value affects on 100 Trying response delay. be careful
#	default: 125

master_pool_size=10
master_check_interval=25
master_max_exceptions=0
master_max_wait=125

# failover_to_slave
#	determines the using of failover for sqlrouter and cdrwriter
#	default: 0
failover_to_slave=0

#same for slave database 
slave_host=127.0.0.1
slave_port=5432
slave_user=routeserver
slave_name=routeserver
slave_pass=routeserver

slave_pool_size=5
slave_check_interval=25
slave_max_exceptions=0
slave_max_wait=125

# max_exceptions
#	number of sql exceptions to restart connection
#	0 means that connection will be restarted on each exceptio
#	default: 0
max_exceptions=0

# getprofile_function
#	function name for getprofile query
#	default: mandatory
#
getprofile_function = getprofile_f

### CDR config ###

# cdr_pool_size
# 	number of connections to open for each cdrwriter instance (master ans slave[if used])
#	default: 10
cdr_pool_size=20

# cdr_check_interval
#	interval for connections checking both for master and slave (if present) in ms
#	default: 5000
#
cdr_check_interval=5000

mastercdr_host=127.0.0.1
mastercdr_port=5432
mastercdr_name=cdrserver
mastercdr_user=cdrserver
mastercdr_pass=cdrserver

slavecdr_host=127.0.0.1
slavecdr_port=5432
slavecdr_name=cdrserver
slavecdr_user=cdrserver
slavecdr_pass=cdrserver

# writecdr_schema
#	schema name for writecdr query
#	default: mandatory
#
# writecdr_function
#	function name for writecdr query
#	default: mandatory
#
writecdr_schema = switch
writecdr_function = writecdr

# failover_to_file
#	enable failover to file on database errors or unavailability
#	default: 1
#
# cdr_dir
#	directory for temporary csv files with CDRs
#	default: mandatory 
#
# cdr_completed_dir
#	directory for completed csv CDR files
#	default: mandatory 
failover_to_file=1
cdr_dir = /var/spool/sems/cdrs
cdr_completed_dir = /var/spool/sems/cdrs/complete

### ProfilesCache config ###
# profiles that obtained from database can be locally cached
#
# profiles_cache_enabled
#	enable local profiles cache
# 	default: 0 
#
# profiles_cache_check_interval
#	interval between checks for obsolete profiles (also check is made before found entry return)
#	default: 30
#	
# profiles_cache_buckets
#	buckets in cache hash table
#	default: 65000 
profiles_cache_enabled = 1
profiles_cache_check_interval = 60
profiles_cache_buckets = 100000

### ResourceCache config ###

#r edis_host
#	redis server host	
#	default: 127.0.0.1
#
# redis_port
#	redis server port
#	default: 6739
#
# redis_size
#	size of redis connection pool
#	default: mandatory
#
# redis_timeout
#	TTL for resource entry in seconds
#	default: madnatory

write_redis_host = 127.0.0.1
write_redis_port = 6379
write_redis_size = 2
write_redis_timeout = 5000000

read_redis_host = 127.0.0.1
read_redis_port = 6379
read_redis_size = 2
read_redis_timeout = 5000000

# reject_on_cache_error
#	pretend that resource check is sucessful on checking internal errors
#	default: 0
reject_on_cache_error = 0

