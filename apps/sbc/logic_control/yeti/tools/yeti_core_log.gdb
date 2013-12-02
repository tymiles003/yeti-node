set height 0
set print null-stop on

set $l = DILog::ring_buf[0]
set $n = DILog::pos
set $i = 0

while $i < $n
        #x/s DILog::ring_buf[$i]
        printf "%s", DILog::ring_buf[$i]
        set $i = $i+1
end

quit
