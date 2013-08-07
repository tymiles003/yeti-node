master_host=127.0.0.1
master_port=5432
master_user=routeserver
master_name=routeserver
master_pass=routeserver
master_pool_size=10
master_check_interval=1000000
master_max_exceptions=0

slave_host=127.0.0.1
slave_port=5432
slave_user=routeserver
slave_name=routeserver
slave_pass=routeserver
slave_pool_size=5
slave_check_interval=1000000
slave_max_exceptions=0

max_exceptions=0
failover_to_slave=0

cdr_pool_size=20
mastercdr_host=127.0.0.1
mastercdr_port=5432
mastercdr_name=routeserver
mastercdr_user=routeserver
mastercdr_pass=routeserver

slavecdr_host=127.0.0.1
slavecdr_port=5432
slavecdr_name=routeserver
slavecdr_user=routeserver
slavecdr_pass=routeserver

profiles_cache_enabled = 1
profiles_cache_check_interval = 60
profiles_cache_buckets = 100000

write_redis_host = 127.0.0.1
write_redis_port = 6379
write_redis_size = 2
write_redis_timeout = 5000000

read_redis_host = 127.0.0.1
read_redis_port = 6379
read_redis_size = 2
read_redis_timeout = 5000000

reject_on_cache_error = 0

used_header_fields = X-LB-NODE,X-LB-POP,X-SRC-IP,X-SRC-PORT 
used_header_fields_separator = "#"
