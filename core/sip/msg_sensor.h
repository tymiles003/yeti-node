#ifndef _msg_sensor_h_
#define _msg_sensor_h_

#include "atomic_types.h"
#include "AmThread.h"
#include "cstring.h"

#include "sys/socket.h"

#include <set>
#include <string>
using std::set;
using std::string;


struct sockaddr_storage;

class msg_sensor
  : public atomic_ref_cnt
{
public:
  msg_sensor() {}
  virtual ~msg_sensor() {}
  virtual int feed(const char* buf, int len,
		  sockaddr_storage* src_ip,
		  sockaddr_storage* dst_ip,
		  cstring method, int reply_code=0)=0;
};

class ipip_msg_sensor
  : public msg_sensor
{
	//fields to create upper IPIP header
	sockaddr_storage sensor_src_ip;
	sockaddr_storage sensor_dst_ip;

//protected:

public:

	int feed(const char* buf, int len,
		sockaddr_storage* src_ip,
		sockaddr_storage* dst_ip,
		cstring method, int reply_code=0);
};

#endif
