#include "ResourceControl.h"
#include "../yeti.h"
#include "AmUtils.h"
#include "AmSession.h"
#include "../db/DbConfig.h"
#include <pqxx/pqxx>

//workaround for callback
static ResourceControl *_instance;

static void on_reconnect_static(void *){
	_instance->on_reconnect();
}

void ResourceControl::handler_info(HandlersIt &i, AmArg &a){
	a["handler"] = i->first;
	i->second.info(a);
}

void ResourceControl::handlers_entry::info(AmArg &a) const {
	a["onwer_tag"] = owner_tag;
	a["valid"] = valid;
	AmArg &r = a["resources"];
	for(ResourceList::const_iterator j = resources.begin(); j!=resources.end();++j){
		r.push(j->print());
	}
}

void ResourceConfig::set_action(int a){
	switch(a){
	case ResourceAction_Reject:
		action = Reject;
		str_action = "Reject";
		break;
	case ResourceAction_NextRoute:
		action = NextRoute;
		str_action = "NextRoute";
		break;
	case ResourceAction_Accept:
		action = Accept;
		str_action = "Accept";
		break;
	default:
		DBG("invalid action type. use Reject instead");
		action = Reject;
	}
}

string ResourceConfig::print() const{
	ostringstream s;
	s << "id: " << id << ", ";
	s << "name: '" << name << "'', ";
	s << "reject: '" << reject_code << " " << reject_reason << "', ";
	s << "action: " << str_action;
	return s.str();
}

ResourceControl::ResourceControl()
{
	_instance = this;
	stat.clear();
}

int ResourceControl::configure(AmConfigReader &cfg){
	db_schema = Yeti::instance()->config.routing_schema;
	reject_on_error = cfg.getParameterInt("reject_on_cache_error",-1);
	if(reject_on_error == -1){
		ERROR("missed 'reject_on_error' parameter");
		return -1;
	}

	configure_db(cfg);

	if(load_resources_config()){
		ERROR("can't load resources config");
		return -1;
	}

	cache.registerReconnectCallback(&on_reconnect_static,NULL);

	return cache.configure(cfg);
}

void ResourceControl::configure_db(AmConfigReader &cfg){
	string prefix("master");
	dbc.cfg2dbcfg(cfg,prefix);
}

void ResourceControl::start(){
//	DBG("%s()",FUNC_NAME);
	cache.start();
}

void ResourceControl::stop(){
	cache.stop();
}

bool ResourceControl::reload(){
	bool ret = true;

	cfg_lock.lock();
	type2cfg.clear();
	if(load_resources_config()){
		ret = false;
	}
	cfg_lock.unlock();

	return ret;
}

bool ResourceControl::invalidate_resources(){
	bool ret = false;

	handlers_lock.lock();

	INFO("ResourceControl::invalidate_resources(): we have %ld handlers to invalidate",
		handlers.size());

	for(Handlers::iterator i = handlers.begin();i!=handlers.end();++i)
		i->second.invalidate();
	ret = cache.init_resources();

	handlers_lock.unlock();
	return ret;
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

int ResourceControl::load_resources_config(){
	map<int,ResourceConfig> _type2cfg;
	try {
		pqxx::result r;
		pqxx::connection c(dbc.conn_str());
		c.set_variable("search_path",db_schema+", public");
			pqxx::work t(c);
				r = t.exec("SELECT * FROM load_resource_types()");
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
				row["action_id"].as<int>()
			);
			_type2cfg.insert(pair<int,ResourceConfig>(id,rc));
		}

		INFO("resources types are loaded successfully. apply changes");

		type2cfg.swap(_type2cfg);

		map<int,ResourceConfig>::const_iterator mi = type2cfg.begin();
		for(;mi!=type2cfg.end();++mi){
			const ResourceConfig &c = mi->second;
			DBG("resource cfg:     <%s>",c.print().c_str());
		}

		return 0;
	} catch(const pqxx::pqxx_exception &e){
		ERROR("pqxx_exception: %s ",e.base().what());
		return 1;
	}
}

void ResourceControl::on_reconnect(){
	ERROR("ResourceControl::on_reconnect(): invalidate resources");
	if(!invalidate_resources()){
		ERROR("can't clear resources. please examine actual state. (all handlers invalidated)");
	}
}

ResourceCtlResponse ResourceControl::get(ResourceList &rl,
						  string &handler,
						  const string &owner_tag,
						  int &reject_code,
						  string &reject_reason)
{
	AmLock l(rl);
	ResourceList::iterator rli;

	if(rl.empty()){
		DBG("empty resources list. do nothing");
		return RES_CTL_OK;
	}
	stat.hits++;

	/*for(ResourceList::const_iterator i = rl.begin();i!=rl.end();++i)
		DBG("ResourceControl::get() before resource: <%s>",(*i).print().c_str());*/

	ResourceResponse ret = cache.get(rl,rli);

	/*for(ResourceList::const_iterator i = rl.begin();i!=rl.end();++i)
		DBG("ResourceControl::get() resource: <%s>",(*i).print().c_str());*/

	switch(ret){
		case RES_SUCC: {
			handler = AmSession::getNewId();
			handlers_lock.lock();
			handlers.insert(std::pair<string,handlers_entry>(handler,
															 handlers_entry(
																 rl,
																 owner_tag)));
			handlers_lock.unlock();
			DBG("ResourceControl::get() return resources handler '%s' for %p",
				handler.c_str(),&rl);
			//TODO: add to internal handlers list
			return RES_CTL_OK;
			break;
		}
		case RES_BUSY: {
			stat.overloaded++;
			cfg_lock.lock();
			map<int,ResourceConfig>::iterator ti = type2cfg.find(rli->type);
			if(ti==type2cfg.end()){
				reject_code = 404;
				reject_reason = "Resource with unknown type "+int2str(rli->type)+" overloaded";
				stat.rejected++;
				return RES_CTL_REJECT;
			} else {
				ResourceConfig &rc  = ti->second;
				DBG("overloaded resource %d:%d action: %s",rli->type,rli->id,rc.str_action.c_str());
				if(rc.action==ResourceConfig::Accept){
					cfg_lock.unlock();
					return RES_CTL_OK;
				} else { /* reject or choose next */
					reject_code = rc.reject_code;
					reject_reason = rc.reject_reason;
					replace(reject_reason,(*rli),rc);
					ResourceConfig::ActionType a = rc.action;
					cfg_lock.unlock();

					if(a==ResourceConfig::NextRoute){
						stat.nextroute++;
						return RES_CTL_NEXT;
					} else {
						stat.rejected++;
						return RES_CTL_REJECT;
					}
				}
			}
			cfg_lock.unlock();
		} break;
		case RES_ERR: {
			stat.errors++;
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

//void ResourceControl::put(ResourceList &rl){
void ResourceControl::put(const string &handler){

	DBG("ResourceControl::put(%s)",handler.c_str());

	if(handler.empty()){
		DBG("ResourceControl::put() empty handler");
		return;
	}

	handlers_lock.lock();
	Handlers::iterator h = handlers.find(handler);
	if(h==handlers.end()){
		handlers_lock.unlock();
		DBG("ResourceControl::put(%s) attempt to free resources using not existent handler",
			 handler.c_str());
		return;
	}

	//!TODO: validate handler. remove if found but invalid.
	handlers_entry &e = h->second;

	if(!e.is_valid()){
		DBG("ResourceControl::put(%s) invalid handler. remove it",
			handler.c_str());
		handlers.erase(h);
		handlers_lock.unlock();
		return;
	}

	if(!e.resources.empty()){
		cache.put(e.resources);
	} else {
		DBG("ResourceControl::put(%p) empty resources list",&e.resources);
	}

	handlers.erase(h);
	handlers_lock.unlock();
}

void ResourceControl::GetConfig(AmArg& ret,bool types_only){
	DBG("types_only = %d, size = %ld",types_only,type2cfg.size());

	if(types_only){
		cfg_lock.lock();
		map<int,ResourceConfig>::const_iterator it = type2cfg.begin();
		for(map<int,ResourceConfig>::const_iterator it = type2cfg.begin();
			it!=type2cfg.end();++it)
		{
			string key = int2str(it->first);

			ret.push(key,AmArg());

			AmArg &p = ret[key];
			const ResourceConfig &c = it->second;
			p["name"] =  c.name;
			p["reject_code"] = c.reject_code;
			p["reject_reason"] = c.reject_reason;
			p["action"] = c.str_action;
		}
		cfg_lock.unlock();
		return;
	}

	ret["db_config"] = dbc.conn_str();
	ret["db_schema"] = db_schema;

	cfg_lock.lock();
		ret.push("cache",AmArg());
		AmArg &u = ret["cache"];
		cache.GetConfig(u);
	cfg_lock.unlock();
}

void ResourceControl::clearStats(){
	stat.clear();
}

void ResourceControl::getStats(AmArg &ret){
	stat.get(ret);
}

void ResourceControl::getResourceState(int type, int id, AmArg &ret){
	if(type!=ANY_VALUE){
		cfg_lock.lock();
		if(type2cfg.find(type)==type2cfg.end()){
			cfg_lock.unlock();
			throw ResourceCacheException("unknown resource type",500);
		}
		cfg_lock.unlock();
	}
	cache.getResourceState(type,id,ret);
}

void ResourceControl::showResources(AmArg &ret){
	handlers_lock.lock();
	for(HandlersIt i = handlers.begin();i!=handlers.end();++i){
		const handlers_entry &e = i->second;
		ret.push(AmArg());
		handler_info(i,ret.back());
	}
	handlers_lock.unlock();
}

void ResourceControl::showResourceByHandler(const string &h, AmArg &ret){
	handlers_lock.lock();

	HandlersIt i = handlers.find(h);
	if(i==handlers.end()){
		handlers_lock.unlock();
		ret.push(404);
		ret.push(AmArg("no such handler"));
		return;
	}

	ret.push(200);
	ret.push(AmArg());
	handler_info(i,ret.back());

	handlers_lock.unlock();
}

void ResourceControl::showResourceByLocalTag(const string &tag, AmArg &ret){
	handlers_lock.lock();

	HandlersIt i = handlers.begin();
	for(;i!=handlers.end();++i){
		const handlers_entry &e = i->second;
		if (e.owner_tag.empty()) continue;
		if(e.owner_tag==tag) break;
	}
	if(i==handlers.end()){
		handlers_lock.unlock();
		ret.push(404);
		ret.push(AmArg("no such handler"));
		return;
	}

	ret.push(200);
	ret.push(AmArg());
	handler_info(i,ret.back());

	handlers_lock.unlock();
}
