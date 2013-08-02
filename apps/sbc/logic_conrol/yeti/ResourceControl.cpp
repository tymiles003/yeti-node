#include "ResourceControl.h"
#include "AmUtils.h"
#include "DbConfig.h"
#include <pqxx/pqxx>

string ResourceConfig::print() const{
	ostringstream s;
	s << "id: " << id << ", ";
	s << "name: '" << name << "'', ";
	s << "reject: '" << reject_code << " " << reject_reason << "', ";
	s << "reject_on_overload: " << reject_on_overload << ", ";
	s << "next_route_overload: " << next_route_on_overload;
	return s.str();
}


ResourceControl::ResourceControl()
{

}

int ResourceControl::configure(AmConfigReader &cfg){
	int ret;
	string prefix("master");
	DbConfig dbc;
	pqxx::result r;

	reject_on_error = cfg.getParameterInt("reject_on_cache_error",-1);
	if(reject_on_error == -1){
		ERROR("missed 'reject_on_error' parameter");
		return -1;
	}

	dbc.cfg2dbcfg(cfg,prefix);
	try {
		pqxx::connection c(dbc.conn_str());
			pqxx::work t(c);
				r = t.exec("SELECT * FROM switch.load_resource_types();");
			t.commit();
		c.disconnect();
		for(pqxx::result::size_type i = 0; i < r.size();++i){
			const pqxx::result::tuple &row = r[i];
			int id =row["id"].as<int>();
			ResourceConfig rc(
				id,
				row["name"].c_str(),
				row["reject_code"].as<int>(),
				row["reject_reason"].c_str(),
				row["reject_on_overload"].as<bool>(),
				row["next_route_on_overload"].as<bool>()
			);
			type2cfg.insert(pair<int,ResourceConfig>(id,rc));
		}
		map<int,ResourceConfig>::const_iterator mi = type2cfg.begin();
		for(;mi!=type2cfg.end();++mi){
			const ResourceConfig &c = mi->second;
			DBG("resource cfg: <%s>",c.print().c_str());
		}
		ret = cache.configure(cfg);
	} catch(const pqxx::pqxx_exception &e){
		ERROR("pqxx_exception: %s ",e.base().what());
		ret = 1;
	}
	return ret;
}

void ResourceControl::start(){
	DBG("%s()",FUNC_NAME);
	cache.start();
}

void ResourceControl::stop(){
	cache.stop();
}

void ResourceControl::replace(string& s, const string& from, const string& to){
	size_t pos = 0;
	while ((pos = s.find(from, pos)) != string::npos) {
		 s.replace(pos, from.length(), to);
		 pos += s.length();
	}
}

void ResourceControl::replace(string &s,Resource &r,ResourceConfig &rc){
	replace(s,"$id",int2str(r.id));
	replace(s,"$type",int2str(r.type));
	replace(s,"$takes",int2str(r.takes));
	replace(s,"$limit",int2str(r.limit));
	replace(s,"$name",rc.name);
}

ResourceCtlResponse ResourceControl::get(ResourceList &rl,
						  int &reject_code,
						  string &reject_reason)
{
	ResourceList::iterator rli;

	if(rl.empty()){
		return RES_CTL_OK;
	}

	ResourceResponse ret = cache.get(rl,rli);
	switch(ret){
		case RES_SUCC: {
			return RES_CTL_OK;
			break;
		}
		case RES_BUSY: {
			map<int,ResourceConfig>::iterator ti = type2cfg.find(rli->type);
			if(ti==type2cfg.end()){
				reject_code = 404;
				reject_reason = "Resource with unknown type overloaded";
			} else {
				ResourceConfig &rc  = ti->second;
				if(rc.reject_on_overload){
					DBG("overloaded resource %d reject it",rc.id);
					reject_code = rc.reject_code;
					reject_reason = rc.reject_reason;
					replace(reject_reason,(*rli),rc);
					return RES_CTL_REJECT;
				} else {
					DBG("overloaded resource %d but no reject on overload",rc.id);
					return RES_CTL_OK;
				}
			}
		} break;
		case RES_ERR: {
			ERROR("cache error reject_on_error = %d",reject_on_error);
			if(reject_on_error){
				reject_code = 503;
				reject_reason = "error 2531";
				return RES_CTL_ERROR;
			}
			return RES_CTL_OK;
		} break;
	}
	return RES_CTL_OK;
}

void ResourceControl::put(ResourceList &rl){
	cache.put(rl);
}
