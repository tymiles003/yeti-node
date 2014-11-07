#include "msg_sensor.h"

#include "AmUtils.h"
#include "sip/raw_sock.h"
#include "ip_util.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <net/if.h>
#include <arpa/inet.h>
#ifndef __USE_BSD
#define __USE_BSD  /* on linux use bsd version of iphdr (more portable) */
#endif /* __USE_BSD */
#include <netinet/ip.h>
#define __FAVOR_BSD /* on linux use bsd version of udphdr (more portable) */
#include <netinet/udp.h>
#include <netdb.h>


// macros for converting values in the expected format
// #if OS == "freebsd" || OS == "netbsd" || OS == "darwin"
/* on freebsd and netbsd the ip offset (along with flags) and the
   ip header length must be filled in _host_ bytes order format.
   The same is true for openbsd < 2.1.
*/
#if defined(RAW_IPHDR_IP_HBO)

/** convert the ip offset in the format expected by the kernel. */
#define RAW_IPHDR_IP_OFF(off) (unsigned short)(off)
/** convert the ip total length in the format expected by the kernel. */
#define RAW_IPHDR_IP_LEN(tlen) (unsigned short)(tlen)

#else /* __OS_* */
/* linux, openbsd >= 2.1 a.s.o. */
/** convert the ip offset in the format expected by the kernel. */
#define RAW_IPHDR_IP_OFF(off)  htons((unsigned short)(off))
/** convert the ip total length in the format expected by the kernel. */
#define RAW_IPHDR_IP_LEN(tlen) htons((unsigned short)(tlen))

#endif /* __OS_* */

#define DEFAULT_IPIP_TTL 64

/* most of helper function is from core/sip/raw_sock.cpp */

/** udp checksum helper: compute the pseudo-header 16-bit "sum".
 * Computes the partial checksum (no complement) of the pseudo-header.
 * It is meant to be used by udpv4_chksum().
 * @param uh - filled udp header
 * @param src - source ip address in network byte order.
 * @param dst - destination ip address in network byte order.
 * @param length - payload length (not including the udp header),
 *                 in _host_ order.
 * @return the partial checksum in host order
 */
inline unsigned short udpv4_vhdr_sum(struct udphdr* uh,
					 struct in_addr* src,
					 struct in_addr* dst,
					 unsigned short length)
{
	unsigned sum;

	/* pseudo header */
	sum=(src->s_addr>>16)+(src->s_addr&0xffff)+
		(dst->s_addr>>16)+(dst->s_addr&0xffff)+
		htons(IPPROTO_UDP)+(uh->uh_ulen);
	/* udp header */
	sum+=(uh->uh_dport)+(uh->uh_sport)+(uh->uh_ulen) + 0 /*chksum*/;
	/* fold it */
	sum=(sum>>16)+(sum&0xffff);
	sum+=(sum>>16);
	/* no complement */
	return ntohs((unsigned short) sum);
}



/** compute the udp over ipv4 checksum.
 * @param u - filled udp header (except checksum).
 * @param src - source ip v4 address, in _network_ byte order.
 * @param dst - destination ip v4 address, int _network_ byte order.
 * @param data - pointer to the udp payload.
 * @param length - payload length, not including the udp header and in
 *                 _host_ order. The length mist be <= 0xffff - 8
 *                 (to allow space for the udp header).
 * @return the checksum in _host_ order */
inline static unsigned short udpv4_chksum(struct udphdr* u,
					  struct in_addr* src,
					  struct in_addr* dst,
					  unsigned char* data,
					  unsigned short length)
{
	unsigned sum;
	unsigned char* end;
	sum=udpv4_vhdr_sum(u, src, dst, length);
	end=data+(length&(~0x1)); /* make sure it's even */
	/* TODO: 16 & 32 bit aligned version */
		/* not aligned */
		for(;data<end;data+=2){
			sum+=((data[0]<<8)+data[1]);
		}
		if (length&0x1)
			sum+=((*data)<<8);

	/* fold it */
	sum=(sum>>16)+(sum&0xffff);
	sum+=(sum>>16);
	return (unsigned short)~sum;
}

/** fill in an udp header.
 * @param u - udp header that will be filled.
 * @param from - source ip v4 address and port.
 * @param to -   destination ip v4 address and port.
 * @param buf - pointer to the payload.
 * @param len - payload length (not including the udp header).
 * @param do_chk - if set the udp checksum will be computed, else it will
 *                 be set to 0.
 * @return 0 on success, < 0 on error.
 */
inline static int mk_udp_hdr(struct udphdr* u,
				 const sockaddr_storage* from,
				 const sockaddr_storage* to,
				 unsigned char* buf, int len,
				 int do_chk)
{
  struct sockaddr_in *from_v4 = (sockaddr_in*)from;
  struct sockaddr_in *to_v4 = (sockaddr_in*)to;

  u->uh_ulen = htons((unsigned short)len+sizeof(struct udphdr));
  u->uh_sport = ((sockaddr_in*)from)->sin_port;
  u->uh_dport = ((sockaddr_in*)to)->sin_port;
  if (do_chk)
	u->uh_sum=htons(udpv4_chksum(u, &from_v4->sin_addr,
				 &to_v4->sin_addr, buf, len));
  else
	u->uh_sum=0; /* no checksum */
  return 0;
}



/** fill in an ip header.
 * Note: the checksum is _not_ computed.
 * WARNING: The ip header length and offset might be filled in
 * _host_ byte order or network byte order (depending on the OS, for example
 *  freebsd needs host byte order for raw sockets with IPHDR_INC, while
 *  linux needs network byte order).
 * @param iph - ip header that will be filled.
 * @param from - source ip v4 address (network byte order).
 * @param to -   destination ip v4 address (network byte order).
 * @param payload len - payload length (not including the ip header).
 * @param proto - protocol.
 * @return 0 on success, < 0 on error.
 */
inline static int mk_ip_hdr(struct ip* iph, struct in_addr* from,
				struct in_addr* to, int payload_len,
				unsigned char proto)
{
	iph->ip_hl = sizeof(struct ip)/4;
	iph->ip_v = 4;
	iph->ip_tos = 0;
	/* on freebsd ip_len _must_ be in _host_ byte order instead
	   of network byte order. On linux the length is ignored (it's filled
	   automatically every time). */
	iph->ip_len = RAW_IPHDR_IP_LEN(payload_len + sizeof(struct ip));
	iph->ip_id = 0; /* 0 => will be filled automatically by the kernel */
	iph->ip_off = 0; /* frag.: first 3 bits=flags=0, last 13 bits=offset */
	iph->ip_ttl = 64;//cfg_get(core, core_cfg, udp4_raw_ttl);
	iph->ip_p = proto;
	iph->ip_src = *from;
	iph->ip_dst = *to;
	iph->ip_sum = 0;

	return 0;
}

static inline void mk_ipip_hdr(struct ip &iph, sockaddr_storage *src, sockaddr_storage *dst){
	//build appropriate upper ip header
	iph.ip_hl = sizeof(struct ip)>>2;
	iph.ip_v = IPVERSION;
	iph.ip_tos = IPTOS_ECN_NOT_ECT;
	iph.ip_len = 0; //must be filled each time before send
	iph.ip_id = 0; /* 0 => will be filled automatically by the kernel */
	iph.ip_off = 0; /* frag.: first 3 bits=flags=0, last 13 bits=offset */
	iph.ip_ttl = DEFAULT_IPIP_TTL;//cfg_get(core, core_cfg, udp4_raw_ttl);
	iph.ip_p = IPPROTO_IPIP;
	iph.ip_src = SAv4(src)->sin_addr;
	iph.ip_dst = SAv4(dst)->sin_addr;
	iph.ip_sum = 0;
}

static inline void upd_ipip_hdr(struct ip &iph, unsigned int len){
	iph.ip_len = RAW_IPHDR_IP_LEN(len);
}


/*ipip_msg_sensor::ipip_msg_sensor()
{}*/

ipip_msg_sensor::~ipip_msg_sensor()
{
	INFO("destroyed ipip_msg_sensor[%p] raw socket %d",this,s);
	if(s!=-1) close(s);
}

int ipip_msg_sensor::init(const char *src_addr, const char *dst_addr,const char *iface)
{
	int ret;
	struct ifreq ifr;

	//DBG("ipip_msg_sensor::init[%p](%s,%s)",this,src_addr,dst_addr);

	//process parameters
	if(!am_inet_pton(src_addr,&sensor_src_ip)){
		ERROR("invalid src address '%s' for ipip_msg_sensor ",src_addr);
		return 1;
	}
	if(!am_inet_pton(dst_addr,&sensor_dst_ip)){
		ERROR("invalid dst address '%s' for ipip_msg_sensor ",dst_addr);
		return 1;
	}

	mk_ipip_hdr(ipip_hdr,&sensor_src_ip,&sensor_dst_ip);

	//open raw socket
	s = raw_socket(IPPROTO_RAW,NULL,0);
	//s = socket(PF_INET, SOCK_RAW, proto);
	if(s==-1){
		ERROR("can't create raw socket for ipip sensor. errno: %d",errno);
		goto error;
	}

	/*if(iface){
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name,iface,IFNAMSIZ);
		ret = setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(struct ifreq));
		if(ret){
			ERROR("can't bind raw socket to interface %s, errno = %d",iface,errno);
			goto error;
		}
	}*/

	return 0;
error:
	if(s!=-1) close(s);
	return 1;
}

int ipip_msg_sensor::feed(const char* buf, int len,
			 sockaddr_storage* from,
			 sockaddr_storage* to,
			 cstring method, int reply_code)
{
	struct msghdr snd_msg;
	struct iovec iov[2];
	struct ipip_udp_hdr {
		struct ip ipip;
		struct ip ip;
		struct udphdr udp;
	} hdr;
	//unsigned int totlen;
	//int ret;


	/*INFO("ipip_msg_sensor::feed(%p,%d,from,to,method,reply_code)",
		buf,len);*/

	hdr.ipip = ipip_hdr;
	//totlen = len+sizeof(hdr);

	//init msg
	memset(&snd_msg, 0, sizeof(snd_msg));
	snd_msg.msg_name=SAv4(&sensor_dst_ip);
	snd_msg.msg_namelen=SA_len(&sensor_dst_ip);
	snd_msg.msg_iov=&iov[0];
	snd_msg.msg_iovlen=2;
	snd_msg.msg_control=0;
	snd_msg.msg_controllen=0;
	snd_msg.msg_flags=0;

	//hdr
	mk_udp_hdr(&hdr.udp, from, to, (unsigned char*)buf, len, 1);
	mk_ip_hdr(&hdr.ip, &SAv4(from)->sin_addr, &SAv4(to)->sin_addr,
		  len + sizeof(hdr.udp), IPPROTO_UDP);
	upd_ipip_hdr(hdr.ipip,hdr.ip.ip_len + sizeof(struct ip));
	iov[0].iov_base=(char*)&hdr;
	iov[0].iov_len=sizeof(hdr);

	//payload
	iov[1].iov_base=(void*)buf;
	iov[1].iov_len=len;

	/*ret = */sendmsg(s, &snd_msg, 0);
	//DBG("ipip_msg_sensor::feed() sendmsg = %d",ret);
	return 0;
}

void ipip_msg_sensor::getInfo(AmArg &ret){
	char addr[NI_MAXHOST];

	am_inet_ntop(&sensor_src_ip,addr,NI_MAXHOST);
	ret["sensor_src_ip"] = addr;

	am_inet_ntop(&sensor_dst_ip,addr,NI_MAXHOST);
	ret["sensor_dst_ip"] = addr;

	ret["socket"] = s;
	ret["references"] = (long int)get_ref(this);
}
