#include "Resource.h"
#include "log.h"
#include "AmUtils.h"
#include <vector>
#include <sstream>

ResourceList resource_parse(const std::string rs){
	ResourceList rl;
	vector<string> lc = explode(rs,";");
	for(vector<string>::const_iterator ri = lc.begin();
		ri != lc.end(); ++ri)
	{
		Resource r;
		vector<string> vc = explode(*ri,":");
		if(vc.size()!=4){
			throw ResourceParseException("invalid format: params count",(*ri));
		}
		if(	str2int(vc[0],r.type) &&
			str2int(vc[1],r.id) &&
			str2int(vc[2],r.limit) &&
			str2int(vc[3],r.takes))
		{
			rl.push_back(r);
		} else {
			DBG("%s() str2int conversion error",FUNC_NAME);
			throw ResourceParseException("invalid format: str2int conversion",(*ri));
		}
	}
	return rl;
}

string resource_print(const Resource &r){
	ostringstream s;
	s << "type: " << r.type << ", ";
	s << "id: " << r.id << ", ";
	s << "limit: " << r.limit << ", ";
	s << "takes: " << r.takes;
	return s.str();
}

