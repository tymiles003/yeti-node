set $l = DILog::ring_buf[0]
set $i = 0
while $l[0] != '\0' && $i < DILog::pos
	printf "%s\n", $l
	set $l = $l+sizeof(DILog::ring_buf[0])
	set $i = $i+1
end
quit
