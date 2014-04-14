#include "Resource.h"
#include "log.h"
#include "AmUtils.h"
#include <vector>
#include <sstream>

#define RES_ATOM_SEPARATOR ";"
#define RES_OPTION_SEPARATOR "|"
#define RES_FIELDS_SEPARATOR ":"

void ResourceList::parse(const std::string rs){
	clear();
	vector<string> lc = explode(rs,RES_ATOM_SEPARATOR);
	for(vector<string>::const_iterator ri = lc.begin();
		ri != lc.end(); ++ri)
	{
		vector<string> ac = explode(*ri,RES_OPTION_SEPARATOR);
		for(vector<string>::const_iterator ai = ac.begin();
			ai != ac.end(); ++ai)
		{
			Resource r;
			vector<string> vc = explode(*ai,RES_FIELDS_SEPARATOR);
			if(vc.size()!=4){
				throw ResourceParseException("invalid format: params count",(*ai));
			}
			if(	str2int(vc[0],r.type) &&
				str2int(vc[1],r.id) &&
				str2int(vc[2],r.limit) &&
				str2int(vc[3],r.takes))
			{
				if(r.limit!=0){	//skip unlimited resources
					r.failover_to_next = true;
					push_back(r);
				}
			} else {
				DBG("%s() str2int conversion error",FUNC_NAME);
				throw ResourceParseException("invalid format: str2int conversion",(*ri));
			}
		}
		back().failover_to_next = false;
	}
}

string Resource::print() const{
	ostringstream s;
	s << "type: " << type << ", ";
	s << "id: " << id << ", ";
	s << "limit: " << limit << ", ";
	s << "takes: " << takes << ", ";
	s << "failover_to_next: " << failover_to_next << ", ";
	s << "active: " << active << ", ";
	s << "taken: " << taken;
	return s.str();
}
