#include "msg_sensor.h"

#include "AmUtils.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>


ipip_msg_sensor::ipip_msg_sensor()
{}

ipip_msg_sensor::~ipip_msg_sensor()
{}

int ipip_msg_sensor::feed(const char* buf, int len,
			 sockaddr_storage* src_ip,
			 sockaddr_storage* dst_ip,
			 cstring method, int reply_code)
{

	return 0;
}

