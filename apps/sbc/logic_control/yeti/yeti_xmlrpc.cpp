#include "yeti.h"
#include "Version.h"
#include "Registration.h"
#include "codecs_bench.h"

#include "sip/transport.h"
#include "AmPlugIn.h"

#include <sys/types.h>
#include <signal.h>

typedef void (Yeti::*YetiRpcHandler)(const AmArg& args, AmArg& ret);

struct xmlrpc_entry: public AmObject {
  YetiRpcHandler handler;
  string leaf_descr,func_descr,arg,arg_descr;
  AmArg leaves;

  xmlrpc_entry(string ld):
	  handler(NULL), leaf_descr(ld) {}

  xmlrpc_entry(string ld, YetiRpcHandler h, string fd):
	  leaf_descr(ld), handler(h), func_descr(fd) {}

  xmlrpc_entry(string ld, YetiRpcHandler h, string fd, string a, string ad):
	  leaf_descr(ld), handler(h), func_descr(fd), arg(a), arg_descr(ad) {}

  bool isMethod(){ return handler!=NULL; }
  bool hasLeafs(){ return leaves.getType()==AmArg::Struct; }
  bool hasLeaf(const char *leaf){ return hasLeafs()&&leaves.hasMember(leaf); }
};

void Yeti::init_xmlrpc_cmds(){
#define reg_leaf(parent,leaf,name,descr) \
	e = new xmlrpc_entry(descr);\
	parent[name] = e;\
	AmArg &leaf = e->leaves;

#define reg_method(parent,name,descr,func,func_descr) \
	e = new xmlrpc_entry(descr,&Yeti::func,func_descr);\
	parent[name] = e;

#define reg_leaf_method(parent,leaf,name,descr,func,func_descr) \
	reg_method(parent,name,descr,func,func_descr);\
	AmArg &leaf = e->leaves;

#define reg_method_arg(parent,name,descr,func,func_descr,arg, arg_descr) \
	e = new xmlrpc_entry(descr,&Yeti::func,func_descr,arg, arg_descr);\
	parent[name] = e;

#define reg_leaf_method_arg(parent,leaf,name,descr,func,func_descr,arg, arg_descr) \
	reg_method_arg(parent,name,descr,func,func_descr,arg, arg_descr);\
	AmArg &leaf = e->leaves;

	xmlrpc_entry *e;
	e = new xmlrpc_entry("root");
	xmlrpc_cmds = e->leaves;
	AmArg &root = xmlrpc_cmds;

	/* show */
	reg_leaf(root,show,"show","read only queries");

		reg_method(show,"version","show version",showVersion,"");

		reg_leaf(show,show_router,"router","active router instance");
			reg_method(show_router,"cache","show callprofile's cache state",ShowCache,"");

			reg_leaf(show_router,show_router_cdrwriter,"cdrwriter","cdrwriter");
				reg_method(show_router_cdrwriter,"opened-files","show opened csv files",showRouterCdrWriterOpenedFiles,"");

		reg_leaf(show,show_media,"media","media processor instance");
			reg_method(show_media,"streams","active media streams info",showMediaStreams,"");

		reg_leaf_method_arg(show,show_calls,"calls","active calls",GetCalls,"show current active calls",
						"<LOCAL-TAG>","retreive call by local_tag");
			reg_method(show_calls,"count","active calls count",GetCallsCount,"");

		reg_method(show,"configuration","actual settings",GetConfig,"");

		reg_method(show,"stats","runtime statistics",GetStats,"");

		reg_method(show,"interfaces","show network interfaces configuration",showInterfaces,"");

		reg_leaf_method(show,show_registrations,"registrations","uac registrations",GetRegistrations,"show configured uac registrations");
			reg_method(show_registrations,"count","active registrations count",GetRegistrationsCount,"");

		reg_leaf(show,show_system,"system","system cmds");
			reg_method(show_system,"log-level","loglevels",showSystemLogLevel,"");
			reg_method(show_system,"status","system alarms and states",showSystemStatus,"");
	/* request */
	reg_leaf(root,request,"request","modify commands");
		reg_leaf(request,request_router,"router","active router instance");

			reg_method(request_router,"reload","reload active instance",reloadRouter,"");

			reg_leaf(request_router,request_router_cdrwriter,"cdrwriter","CDR writer instance");
				reg_method(request_router_cdrwriter,"close-files","immideatly close failover csv files",closeCdrFiles,"");

			reg_leaf(request_router,request_router_translations,"translations","disconnect/internal_db codes translator");
				reg_method(request_router_translations,"reload","reload translator",reloadTranslations,"");

			reg_leaf(request_router,request_router_codec_groups,"codec-groups","codecs groups configuration");
				reg_method(request_router_codec_groups,"reload","reload codecs-groups",reloadCodecsGroups,"");

			reg_leaf(request_router,request_router_resources,"resources","resources actions configuration");
				reg_method(request_router_resources,"reload","reload resources",reloadResources,"");

			reg_leaf(request_router,request_router_cache,"cache","callprofile's cache");
				reg_method(request_router_cache,"clear","clear cached profiles",ClearCache,"");

		reg_leaf(request,request_registrations,"registrations","uac registrations");
			reg_method(request_registrations,"reload","reload reqistrations preferences",reloadRegistrations,"");
			reg_method_arg(request_registrations,"renew","renew registration",RenewRegistration,
						   "","<ID>","renew registration by id");

		reg_leaf(request,request_stats,"stats","runtime statistics");
			reg_method(request_stats,"clear","clear all counters",ClearStats,"");

		reg_leaf(request,request_call,"call","active calls control");
			reg_method_arg(request_call,"disconnect","drop call",DropCall,
						   "","<LOCAL-TAG>","drop call by local_tag");

		reg_leaf(request,request_media,"media","media processor instance");
			reg_method_arg(request_media,"payloads","loaded codecs",showPayloads,"show supported codecs",
						   "benchmark","compute transcoding cost for each codec");

		reg_leaf(request,request_system,"system","system commands");

			reg_leaf_method(request_system,request_system_shutdown,"shutdown","shutdown switch",
							requestSystemShutdown,"unclean shutdown");
				reg_method(request_system_shutdown,"immediate","don't wait for active calls",
						   requestSystemShutdownImmediate,"");
				reg_method(request_system_shutdown,"graceful","disable new calls, wait till active calls end",
						   requestSystemShutdownGraceful,"");
				reg_method(request_system_shutdown,"cancel","cancel graceful shutdown",
						   requestSystemShutdownCancel,"");

			reg_leaf(request_system,request_system_log,"log","logging facilities control");
				reg_leaf(request_system_log,request_system_log_di_log,"di_log","memory ringbuffer logging facility");
					reg_method_arg(request_system_log_di_log,"dump","",requestSystemLogDump,
								   "","<path>","save memory log to path");
	/* set */
	reg_leaf(root,lset,"set","set");
		reg_leaf(lset,set_system,"system","system commands");
			reg_leaf(set_system,set_system_log_level,"log-level","logging facilities level");
				reg_method_arg(set_system_log_level,"di_log","",setSystemLogDiLogLevel,
							   "","<log_level>","set new log level");
				reg_method_arg(set_system_log_level,"syslog","",setSystemLogSyslogLevel,
							   "","<log_level>","set new log level");

#undef reg_leaf
#undef reg_method
#undef reg_leaf_method
#undef reg_method_arg
#undef reg_leaf_method_arg
}

void Yeti::process_xmlrpc_cmds(const AmArg cmds, const string& method, const AmArg& args, AmArg& ret){
	const char *list_method = "_list";
	//DBG("process_xmlrpc_cmds(%p,%s,...)",&cmds,method.c_str());
	if(method==list_method){
		ret.assertArray();
		switch(cmds.getType()){
			case AmArg::Struct: {
				AmArg::ValueStruct::const_iterator it = cmds.begin();
				for(;it!=cmds.end();++it){
					const AmArg &am_e = it->second;
					xmlrpc_entry *e = reinterpret_cast<xmlrpc_entry *>(am_e.asObject());
					AmArg f;
					f.push(it->first);
					f.push(e->leaf_descr);
					ret.push(f);
				}
			} break;

			case AmArg::AObject: {
				xmlrpc_entry *e = reinterpret_cast<xmlrpc_entry *>(cmds.asObject());
				if(!e->func_descr.empty()&&(!e->arg.empty()||e->hasLeafs())){
					AmArg f;
					f.push("[Enter]");
					f.push(e->func_descr);
					ret.push(f);
				}
				if(!e->arg.empty()){
					AmArg f;
					f.push(e->arg);
					f.push(e->arg_descr);
					ret.push(f);
				}
				if(e->hasLeafs()){
					const AmArg &l = e->leaves;
					AmArg::ValueStruct::const_iterator it = l.begin();
					for(;it!=l.end();++it){
						const AmArg &am_e = it->second;
						xmlrpc_entry *e = reinterpret_cast<xmlrpc_entry *>(am_e.asObject());
						AmArg f;
						f.push(it->first);
						f.push(e->leaf_descr);
						ret.push(f);
					}
				}
			} break;

			default:
				throw AmArg::TypeMismatchException();
		}
		return;
	}

	if(cmds.hasMember(method)){
		const AmArg &l = cmds[method];
		if(l.getType()!=AmArg::AObject)
			throw AmArg::TypeMismatchException();

		xmlrpc_entry *e = reinterpret_cast<xmlrpc_entry *>(l.asObject());
		if(args.size()>0){
			if(e->hasLeaf(args[0].asCStr())){
				AmArg nargs = args,sub_method;
				nargs.pop(sub_method);
				process_xmlrpc_cmds(e->leaves,sub_method.asCStr(),nargs,ret);
				return;
			} else if(args[0]==list_method){
				AmArg nargs = args,sub_method;
				nargs.pop(sub_method);
				process_xmlrpc_cmds(l,sub_method.asCStr(),nargs,ret);
				return;
			}
		}
		if(e->isMethod()){
			if(args.size()&&strcmp(args.back().asCStr(),list_method)==0){
				if(!e->hasLeafs())
					ret.assertArray();
				return;
			}
			(this->*(e->handler))(args,ret);
			return;
		}
		throw AmDynInvoke::NotImplemented("missed arg");
	}
	throw AmDynInvoke::NotImplemented("no matches with methods tree");
}

void Yeti::invoke(const string& method, const AmArg& args, AmArg& ret)
{
	DBG("Yeti: %s(%s)\n", method.c_str(), AmArg::print(args).c_str());

	if(method == "getLogicInterfaceHandler"){
		SBCLogicInterface *i = (SBCLogicInterface *)this;
		ret[0] = (AmObject *)i;
	} else if(method == "getExtendedInterfaceHandler"){
		ExtendedCCInterface *i = (ExtendedCCInterface *)this;
		ret[0] = (AmObject *)i;
	} else if(method == "start"){
		SBCCallProfile* call_profile =
			dynamic_cast<SBCCallProfile*>(args[CC_API_PARAMS_CALL_PROFILE].asObject());
		start(
			args[CC_API_PARAMS_CC_NAMESPACE].asCStr(),
			args[CC_API_PARAMS_LTAG].asCStr(),
			call_profile,
			args[CC_API_PARAMS_TIMESTAMPS][CC_API_TS_START_SEC].asInt(),
			args[CC_API_PARAMS_TIMESTAMPS][CC_API_TS_START_USEC].asInt(),
			args[CC_API_PARAMS_CFGVALUES],
			args[CC_API_PARAMS_TIMERID].asInt(),
			ret
		);
	} else if(method == "connect"){
		SBCCallProfile* call_profile =
			dynamic_cast<SBCCallProfile*>(args[CC_API_PARAMS_CALL_PROFILE].asObject());
		connect(
			args[CC_API_PARAMS_CC_NAMESPACE].asCStr(),
			args[CC_API_PARAMS_LTAG].asCStr(),
			call_profile,
			args[CC_API_PARAMS_OTHERID].asCStr(),
			args[CC_API_PARAMS_TIMESTAMPS][CC_API_TS_CONNECT_SEC].asInt(),
			args[CC_API_PARAMS_TIMESTAMPS][CC_API_TS_CONNECT_USEC].asInt()
		);
	} else if(method == "end"){
		SBCCallProfile* call_profile =
			dynamic_cast<SBCCallProfile*>(args[CC_API_PARAMS_CALL_PROFILE].asObject());
		end(
			args[CC_API_PARAMS_CC_NAMESPACE].asCStr(),
			args[CC_API_PARAMS_LTAG].asCStr(),
			call_profile,
			args[CC_API_PARAMS_TIMESTAMPS][CC_API_TS_END_SEC].asInt(),
			args[CC_API_PARAMS_TIMESTAMPS][CC_API_TS_END_USEC].asInt()
		);
	} else if(method == "ood_handling_terminated"){
		oodHandlingTerminated(
			dynamic_cast<AmSipRequest*>(args[1].asObject()),
			dynamic_cast<SqlCallProfile*>(args[0].asObject())
		);
	} else if(method == "route"){
		//
	} else if (method == "dropCall"){
		INFO ("dropCall received via xmlrpc2di");
		DropCall(args,ret);
	} else if (method == "getCall"){
		INFO ("getCall received via xmlrpc2di");
		GetCall(args,ret);
	} else if (method == "getCalls"){
		INFO ("getCalls received via xmlrpc2di");
		GetCalls(args,ret);
	} else if (method == "getCallsCount"){
		INFO ("getCallsCount received via xmlrpc2di");
		GetCallsCount(args,ret);
	} else if (method == "getStats"){
		INFO ("getStats received via xmlrpc2di");
		GetStats(args,ret);
	} else if (method == "clearStats"){
		INFO ("clearStats received via xmlrpc2di");
		ClearStats(args,ret);
	} else if (method == "showCache"){
		INFO ("showCache received via xmlrpc2di");
		ShowCache(args,ret);
	} else if (method == "clearCache"){
		INFO ("clearCache received via xmlrpc2di");
		ClearCache(args,ret);
	} else if (method == "getRegistration"){
		INFO("getRegistration via xmlrpc2di");
		GetRegistration(args,ret);
	} else if (method == "renewRegistration"){
		INFO("renewRegistration via xmlrpc2di");
		RenewRegistration(args,ret);
	} else if (method == "getRegistrations"){
		INFO("getRegistrations via xmlrpc2di");
		GetRegistrations(args,ret);
	} else if (method == "getRegistrationsCount"){
		INFO("getRegistrationsCount via xmlrpc2di");
		GetRegistrationsCount(args,ret);
	} else if (method == "getConfig"){
		INFO ("getConfig received via xmlrpc2di");
		GetConfig(args,ret);
	} else if (method == "showVersion"){
		INFO ("showVersion received via xmlrpc2di");
		showVersion(args, ret);
	} else if(method == "reload"){
		INFO ("reload received via xmlrpc2di");
		reload(args,ret);
	} else if(method == "closeCdrFiles"){
		INFO ("closeCdrFiles received via xmlrpc2di");
		closeCdrFiles(args,ret);
	/*} else if(method == "_list"){
		ret.push(AmArg("showVersion"));
		ret.push(AmArg("getConfig"));
		ret.push(AmArg("getStats"));
		ret.push(AmArg("clearStats"));
		ret.push(AmArg("clearCache"));
		ret.push(AmArg("showCache"));
		ret.push(AmArg("dropCall"));
		ret.push(AmArg("getCall"));
		ret.push(AmArg("getCalls"));
		ret.push(AmArg("getCallsCount"));
		ret.push(AmArg("getRegistration"));
		ret.push(AmArg("renewRegistration"));
		ret.push(AmArg("getRegistrations"));
		ret.push(AmArg("getRegistrationsCount"));
		ret.push(AmArg("reload"));
		ret.push(AmArg("closeCdrFiles"));

		ret.push(AmArg("show"));
		ret.push(AmArg("request"));
		//ret.push(AmArg("set"));*/
	} else {
		process_xmlrpc_cmds(xmlrpc_cmds,method,args,ret);
	}/* else {
		throw AmDynInvoke::NotImplemented(method);
	}*/
}

/****************************************
 * 				aux funcs				*
 ****************************************/

bool Yeti::reload_config(AmArg &ret){
	cfg = AmConfigReader();
	if(!read_config()){
		ret.push(500);
		ret.push("config file reload failed");
		return false;
	}
	return true;
}

bool Yeti::check_event_id(int event_id,AmArg &ret){
	bool succ = false;
	try {
		DbConfig dbc;
		string prefix("master");
		dbc.cfg2dbcfg(cfg,prefix);
		pqxx::connection c(dbc.conn_str());
		c.set_variable("search_path",
					   Yeti::instance()->config.routing_schema+", public");
		pqxx::prepare::declaration d = c.prepare("check_event","SELECT * from check_event($1)");
			d("integer",pqxx::prepare::treat_direct);
		pqxx::nontransaction t(c);
			pqxx::result r = t.prepared("check_event")(event_id).exec();
		if(r[0][0].as<bool>(false)){
			DBG("event_id checking succ");
			succ = true;
		} else {
			WARN("no appropriate id in database");
			ret.push(503);
			ret.push(AmArg("no such event_id"));
		}
	} catch(pqxx::pqxx_exception &e){
		DBG("e = %s",e.base().what());
		ret.push(500);
		ret.push(AmArg(string("can't check event id in database ")+e.base().what()));
	} catch(...){
		ret.push(500);
		ret.push(AmArg("can't check event id in database"));
	}
	return succ;
}

bool Yeti::assert_event_id(const AmArg &args,AmArg &ret){
	if(args.size()){
		int event_id;
		args.assertArrayFmt("s");
		if(!str2int(args[1].asCStr(),event_id)){
			ret.push(500);
			ret.push(AmArg("invalid event id"));
			return false;
		}
		if(!check_event_id(event_id,ret))
				return false;
	}
	return true;
}

/****************************************
 * 				xmlrpc handlers			*
 ****************************************/

void Yeti::GetCallsCount(const AmArg& args, AmArg& ret) {
	string r;
	std::stringstream ss;

	ss << cdr_list.get_count();
	r = ss.str();

	ret.push(200);
	ret.push(AmArg(r));
}

void Yeti::GetCall(const AmArg& args, AmArg& ret) {
	AmArg call;
	string local_tag;

	if (!args.size()) {
		ret.push(500);
		ret.push("Parameters error: expected local tag of requested cdr ");
		return;
	}

	local_tag = args[0].asCStr();
	if(cdr_list.getCall(local_tag,call,router)){
		ret.push(200);
		ret.push(call);
	} else {
		ret.push(404);
		ret.push("Have no CDR with such local tag");
	}
}

void Yeti::GetCalls(const AmArg& args, AmArg& ret) {
	AmArg calls;

	if(args.size()){
		string local_tag = args[0].asCStr();
		if(!cdr_list.getCall(local_tag,calls,router)){
			ret.push(404);
			ret.push("Have no CDR with such local tag");
			return;
		}
	} else {
		cdr_list.getCalls(calls,calls_show_limit,router);
	}

	ret.push(200);
	ret.push(calls);
}

void Yeti::GetRegistration(const AmArg& args, AmArg& ret){
	AmArg reg;
	string reg_id_str;
	int reg_id;

	if (!args.size()) {
		ret.push(500);
		ret.push(AmArg("Parameters error: expected id of requested registration"));
		return;
	}

	reg_id_str = args[0].asCStr();
	if(!str2int(reg_id_str,reg_id)){
		ret.push(500);
		ret.push(AmArg("Non integer value passed as registrations id"));
		return;
	}

	if(Registration::instance()->get_registration_info(reg_id,reg)){
		ret.push(200);
		ret.push(reg);
	} else {
		ret.push(404);
		ret.push("Have no registration with such id");
	}
}

void Yeti::RenewRegistration(const AmArg& args, AmArg& ret){
	string reg_id_str;
	int reg_id;

	if (!args.size()) {
		ret.push(500);
		ret.push(AmArg("Parameters error: expected id of active registration"));
		return;
	}

	reg_id_str = args[0].asCStr();
	if(!str2int(reg_id_str,reg_id)){
		ret.push(500);
		ret.push(AmArg("Non integer value passed as registrations id"));
		return;
	}

	if(Registration::instance()->reregister(reg_id)){
		ret.push(200);
		ret.push(AmArg("OK"));
	} else {
		ret.push(404);
		ret.push("Have no registration with such id and in appropriate state");
	}
}

void Yeti::GetRegistrations(const AmArg& args, AmArg& ret){
	AmArg regs;

	Registration::instance()->list_registrations(regs);

	ret.push(200);
	ret.push(regs);
}

void Yeti::GetRegistrationsCount(const AmArg& args, AmArg& ret){
	ret.push(200);
	ret.push(Registration::instance()->get_registrations_count());
}

void Yeti::ClearStats(const AmArg& args, AmArg& ret){
	AmLock l(router_mutex);
	if(router)
		router->clearStats();
	rctl.clearStats();
	ret.push(200);
	ret.push("OK");
}

void Yeti::ClearCache(const AmArg& args, AmArg& ret){
	AmLock l(router_mutex);
	if(router)
		router->clearCache();
	ret.push(200);
	ret.push("OK");
}

void Yeti::ShowCache(const AmArg& args, AmArg& ret){
	AmLock l(router_mutex);
	if(router)
		router->showCache(ret);
}

void Yeti::GetStats(const AmArg& args, AmArg& ret){
	AmArg stats,u;
	time_t now;
	ret.push(200);

	/* Yeti stats */
	stats["calls_show_limit"] = (int)calls_show_limit;
	now = time(NULL);
	stats["localtime"] = now;
	stats["uptime"] = difftime(now,start_time);

	/* sql_router stats */
	router_mutex.lock();
	AmArg routers_stats;
	stats["active_routers_count"] = (long int)routers.size();
	set<SqlRouter *>::const_iterator i = routers.begin();
	for(;i!=routers.end();++i){
		u.clear();
		SqlRouter *r = *i;
		if(r){
			r->getStats(u);
			if(r == router){
				routers_stats.push("active",u);
			} else {
				routers_stats.push("old",u);
			}
		}
	}
	router_mutex.unlock();
	stats.push("routers",routers_stats);

	u.clear();
	AmSessionContainer::instance()->getStats(u);
	stats.push("AmSessionContainer",u);

	u.clear();
	u["SessionNum"] = (int)AmSession::getSessionNum();
	u["MaxSessionNum"] = (int)AmSession::getMaxSessionNum();
	u["AvgSessionNum"] = (int)AmSession::getAvgSessionNum();
	stats.push("AmSession",u);


	u.clear();
	const trans_stats &tstats = trans_layer::instance()->get_stats();
	u["rx_replies"] = (long)tstats.get_received_replies();
	u["tx_replies"] = (long)tstats.get_sent_replies();
	u["tx_replies_retrans"] = (long)tstats.get_sent_reply_retrans();
	u["rx_requests"] =(long) tstats.get_received_requests();
	u["tx_requests"] = (long)tstats.get_sent_requests();
	u["tx_requests_retrans"] = (long)tstats.get_sent_request_retrans();
	stats.push("trans_layer",u);

	u.clear();
	rctl.getStats(u);
	stats.push("resource_control",u);

	u.clear();
	CodesTranslator::instance()->getStats(u);
	stats.push("translator",u);

	ret.push(stats);
}

void Yeti::GetConfig(const AmArg& args, AmArg& ret) {
	AmArg u,s;
	ret.push(200);

	s["calls_show_limit"] = calls_show_limit;
	s["node_id"] = config.node_id;
	s["pop_id"] = config.pop_id;

	router_mutex.lock();
	if(router){
		router->getConfig(u);
		s.push("router",u);
	}
	router_mutex.unlock();

	u.clear();
	CodesTranslator::instance()->GetConfig(u);
	s.push("translator",u);

	u.clear();
	rctl.GetConfig(u);
	s.push("resources_control",u);

	u.clear();
	CodecsGroups::instance()->GetConfig(u);
	s.push("codecs_groups",u);

	ret.push(s);
}

void Yeti::DropCall(const AmArg& args, AmArg& ret){
	SBCControlEvent* evt;
	string local_tag;

	if (!args.size()) {
		ret.push(500);
		ret.push("Parameters error: expected local tag of active call");
		return;
	}
	local_tag = args[0].asCStr();

	evt = new SBCControlEvent("teardown");

	if (!AmSessionContainer::instance()->postEvent(local_tag, evt)) {
		ret.push(404);
		ret.push("Not found");
	} else {
		ret.push(202);
		ret.push("Accepted");
	}
}

void Yeti::showVersion(const AmArg& args, AmArg& ret) {
		AmArg p;

		ret.push(200);
		p["build"] = YETI_VERSION;
		p["build_commit"] = YETI_COMMIT;
		p["compiled_at"] = YETI_BUILD_DATE;
		p["compiled_by"] = YETI_BUILD_USER;
		ret.push(p);
}

/* obsolete function !!! */
void Yeti::reload(const AmArg& args, AmArg& ret){
	if(0==args.size()){
		ret.push(400);
		ret.push(AmArg());
		ret[1].push("resources");
		ret[1].push("translations");
		ret[1].push("registrations");
		ret[1].push("codecs_groups");
		ret[1].push("router");
		return;
	}
	args.assertArrayFmt("s");

	if(args.size()>1){
		int event_id;
		args.assertArrayFmt("ss");
		if(!str2int(args[1].asCStr(),event_id)){
			ret.push(500);
			ret.push(AmArg("invalid event id"));
			return;
		} else {
			DBG("we have event_id = %d",event_id);
			//check it
			if(!check_event_id(event_id,ret))
				return;
		}
	}

	if(!reload_config(ret))
		return;

	string action = args[0].asCStr();
	if(action=="resources"){
		rctl.configure_db(cfg);
		if(rctl.reload()){
			ret.push(200);
			ret.push("OK");
		} else {
			ret.push(500);
			ret.push("errors during resources config reload. there is empty resources config now");
		}
	} else if(action == "translations"){
		CodesTranslator::instance()->configure_db(cfg);
		if(CodesTranslator::instance()->reload()){
			ret.push(200);
			ret.push("OK");
		} else {
			ret.push(500);
			ret.push("errors during translations config reload. there is empty translation hashes now");
		}
	} else if(action == "registrations"){
		if(0==Registration::instance()->reload(cfg)){
			ret.push(200);
			ret.push("OK");
		} else {
			ret.push(500);
			ret.push("errors during registrations config reload. there is empty registrations list now");
		}
	} else if(action=="codecs_groups"){
		CodecsGroups::instance()->configure_db(cfg);
		if(CodecsGroups::instance()->reload()){
			ret.push(200);
			ret.push("OK");
		} else {
			ret.push(500);
			ret.push("errors during codecs groups reload. there is empty resources config now");
		}
	} else if(action == "router"){
		//create & configure & run new instance
		INFO("Allocate new SqlRouter instance");
		SqlRouter *r =new SqlRouter();

		INFO("Configure SqlRouter");
		if (r->configure(cfg)){
			ERROR("SqlRouter confgiure failed");
			delete r;
			ret.push(500);
			ret.push("SqlRouter confgiure failed");
			return;
		}

		INFO("Run SqlRouter");
		if(r->run()){
			ERROR("SqlRouter start failed");
			delete r;
			ret.push(500);
			ret.push("SqlRouter start failed");
			return;
		}

		INFO("replace current SqlRouter instance with newly created");
		router_mutex.lock();
			router->release(routers); //mark it or delete (may be deleted now if unused)
			//replace main router pointer
			//(old pointers still available throught existent CallCtx instances)
			router = r;
			routers.insert(router);
		router_mutex.unlock();
		INFO("SqlRouter reload successfull");
		ret.push(200);
		ret.push("OK");
	} else {
		ret.push(400);
		ret.push("unknown action");
	}
}

void Yeti::reloadResources(const AmArg& args, AmArg& ret){
	if(!assert_event_id(args,ret))
		return;
	rctl.configure_db(cfg);
	if(rctl.reload()){
		ret.push(200);
		ret.push("OK");
	} else {
		ret.push(500);
		ret.push("errors during resources config reload. there is empty resources config now");
	}
}

void Yeti::reloadTranslations(const AmArg& args, AmArg& ret){
	if(!assert_event_id(args,ret))
		return;
	CodesTranslator::instance()->configure_db(cfg);
	if(CodesTranslator::instance()->reload()){
		ret.push(200);
		ret.push("OK");
	} else {
		ret.push(500);
		ret.push("errors during translations config reload. there is empty translation hashes now");
	}
}

void Yeti::reloadRegistrations(const AmArg& args, AmArg& ret){
	if(!assert_event_id(args,ret))
		return;
	if(0==Registration::instance()->reload(cfg)){
		ret.push(200);
		ret.push("OK");
	} else {
		ret.push(500);
		ret.push("errors during registrations config reload. there is empty registrations list now");
	}
}

void Yeti::reloadCodecsGroups(const AmArg& args, AmArg& ret){
	if(!assert_event_id(args,ret))
		return;
	CodecsGroups::instance()->configure_db(cfg);
	if(CodecsGroups::instance()->reload()){
		ret.push(200);
		ret.push("OK");
	} else {
		ret.push(500);
		ret.push("errors during codecs groups reload. there is empty resources config now");
	}
}

void Yeti::reloadRouter(const AmArg& args, AmArg& ret){
	if(!assert_event_id(args,ret))
		return;
	INFO("Allocate new SqlRouter instance");
	SqlRouter *r = new SqlRouter();

	INFO("Configure SqlRouter");
	if (r->configure(cfg)){
		ERROR("SqlRouter confgiure failed");
		delete r;
		ret.push(500);
		ret.push("SqlRouter confgiure failed");
		return;
	}

	INFO("Run SqlRouter");
	if(r->run()){
		ERROR("SqlRouter start failed");
		delete r;
		ret.push(500);
		ret.push("SqlRouter start failed");
		return;
	}

	INFO("replace current SqlRouter instance with newly created");
	router_mutex.lock();
		router->release(routers); //mark it or delete (may be deleted now if unused)
		//replace main router pointer
		//(old pointers still available throught existent CallCtx instances)
		router = r;
		routers.insert(router);
	router_mutex.unlock();

	INFO("SqlRouter reload successfull");
	ret.push(200);
	ret.push("OK");
}

void Yeti::closeCdrFiles(const AmArg& args, AmArg& ret){
	router_mutex.lock();
		set<SqlRouter *>::const_iterator i = routers.begin();
		for(;i!=routers.end();++i){
			if(*i) (*i)->closeCdrFiles();
		}
	router_mutex.unlock();
	ret.push(200);
	ret.push("OK");
}

void Yeti::showMediaStreams(const AmArg& args, AmArg& ret){
	AmMediaProcessor::instance()->getInfo(ret);
}

void Yeti::showPayloads(const AmArg& args, AmArg& ret){
	vector<SdpPayload> payloads;
	unsigned char *buf;
	int size = 0;

	bool compute_cost = args.size() && args[0] == "benchmark";
	string path = args.size()>1 ? args[1].asCStr() : DEFAULT_BECH_FILE_PATH;

	const AmPlugIn* plugin = AmPlugIn::instance();
	plugin->getPayloads(payloads);

	if(compute_cost){
		size = load_testing_source(path,buf);
		compute_cost = size > 0;
	}

	AmArg p_list;
	vector<SdpPayload>::const_iterator it = payloads.begin();
	for(;it!=payloads.end();++it){
		const SdpPayload &p = *it;
		AmArg a;
		a["payload_type"] = p.payload_type;
		a["clock_rate"] = p.clock_rate;
		if(compute_cost){
			get_codec_cost(p.payload_type,buf,size,a);
		}
		p_list.push(p.encoding_name,a);
	}

	if(compute_cost)
		delete[] buf;

	ret.push(200);
	ret.push(p_list);
}

void Yeti::showInterfaces(const AmArg& args, AmArg& ret){
	AmArg ifaces,rtp,sig;

	for(int i=0; i<(int)AmConfig::SIP_Ifs.size(); i++) {
		AmConfig::SIP_interface& iface = AmConfig::SIP_Ifs[i];
		AmArg am_iface;
		am_iface["sys_name"] = iface.NetIf;
		am_iface["sys_idx"] = (int)iface.NetIfIdx;
		am_iface["local_ip"] = iface.LocalIP;
		am_iface["local_port"] = (int)iface.LocalPort;
		am_iface["public_ip"] = iface.PublicIP;
		am_iface["use_raw_sockets"] = (iface.SigSockOpts&trsp_socket::use_raw_sockets)!= 0;
		am_iface["force_via_address"] = (iface.SigSockOpts&trsp_socket::force_via_address) != 0;
		am_iface["force_outbound_if"] = (iface.SigSockOpts&trsp_socket::force_outbound_if) != 0;
		sig[iface.name] = am_iface;
	}
	ifaces["sip"] = sig;
	for(multimap<string,unsigned short>::iterator it = AmConfig::LocalSIPIP2If.begin();
		it != AmConfig::LocalSIPIP2If.end(); ++it) {
		AmConfig::SIP_interface& iface = AmConfig::SIP_Ifs[it->second];
		ifaces["sip_map"][it->first] = iface.name.empty() ? "default" : iface.name;
	}

	for(int i=0; i<(int)AmConfig::RTP_Ifs.size(); i++) {
		AmConfig::RTP_interface& iface = AmConfig::RTP_Ifs[i];
		AmArg am_iface;
		am_iface["sys_name"] = iface.NetIf;
		am_iface["sys_idx"] = (int)iface.NetIfIdx;
		am_iface["local_ip"] = iface.LocalIP;
		am_iface["public_ip"] = iface.PublicIP;
		am_iface["rtp_low_port"] = iface.RtpLowPort;
		am_iface["rtp_high_port"] = iface.RtpHighPort;
		am_iface["use_raw_sockets"] = (iface.MediaSockOpts&trsp_socket::use_raw_sockets)!= 0;
		string name = iface.name.empty() ? "default" : iface.name;
		rtp[name] = am_iface;
	}
	ifaces["media"] = rtp;

	ret.push(200);
	ret.push(ifaces);
}

void Yeti::showRouterCdrWriterOpenedFiles(const AmArg& args, AmArg& ret){
	AmArg r;
	router_mutex.lock();
		set<SqlRouter *>::const_iterator i = routers.begin();
		for(;i!=routers.end();++i){
			if(*i) {
				AmArg a;
				ostringstream ss;
				ss << hex << *i;
				(*i)->showOpenedFiles(a);
				r[ss.str()] = a;
			}
		}
	router_mutex.unlock();
	ret.push(200);
	ret.push(r);
}

void Yeti::requestSystemLogDump(const AmArg& args, AmArg& ret){
	if(!args.size()){
		ret.push(500);
		ret.push("missed path for dump");
		return;
	}

	AmDynInvokeFactory* di_log = AmPlugIn::instance()->getFactory4Di("di_log");
	if(0==di_log){
		ret.push(404);
		ret.push("di_log module not loaded");
		return;
	}

	ret.push(200);
	di_log->getInstance()->invoke("dumplogtodisk",args,ret);
}

static void addLoggingFacilityLogLevel(AmArg& ret,const string &facility_name){
	AmLoggingFacility* fac = AmPlugIn::instance()->getFactory4LogFaclty(facility_name);
	if(0==fac)
		return;
	ret[fac->getName()] = fac->getLogLevel();
}

static void setLoggingFacilityLogLevel(const AmArg& args, AmArg& ret,const string &facility_name){
	int log_level;
	if(!args.size()){
		ret.push(500);
		ret.push("missed new log_level");
		return;
	}
	args.assertArrayFmt("s");
	if(!str2int(args.get(0).asCStr(),log_level)){
		ret.push(500);
		ret.push("invalid log_level fmt");
		return;
	}

	AmLoggingFacility* fac = AmPlugIn::instance()->getFactory4LogFaclty(facility_name);
	if(0==fac){
		ret.push(404);
		ret.push("logging facility not loaded");
		return;
	}

	fac->setLogLevel(log_level);

	ret.push(200);
	ret.push("OK");
}

void Yeti::showSystemLogLevel(const AmArg& args, AmArg& ret){
	AmArg r,fac;

	r["log_level"] = log_level;

	addLoggingFacilityLogLevel(fac,"syslog");
	addLoggingFacilityLogLevel(fac,"di_log");
	r["facilities"] = fac;

	ret.push(200);
	ret.push(r);
}

void Yeti::setSystemLogSyslogLevel(const AmArg& args, AmArg& ret){
	setLoggingFacilityLogLevel(args,ret,"syslog");
}

void Yeti::setSystemLogDiLogLevel(const AmArg& args, AmArg& ret){
	setLoggingFacilityLogLevel(args,ret,"di_log");
}

void Yeti::showSystemStatus(const AmArg& args, AmArg& ret){
	AmArg s;

	s["shutdown_mode"] = (bool)AmConfig::ShutdownMode;
	ret.push(200);
	ret.push(s);

}

inline void graceful_suicide(){
	kill(getpid(),SIGINT);
}

inline void immediate_suicide(){
	kill(getpid(),SIGTERM);
}

static void set_system_shutdown(bool shutdown){
	AmConfig::ShutdownMode = shutdown;
	INFO("ShutDownMode changed to %d",AmConfig::ShutdownMode);

	if(AmConfig::ShutdownMode&&!AmSession::getSessionNum()){
		//commit suicide immediatly
		INFO("no active session on graceful shutdown command. exit immediatly");
		graceful_suicide();
	}
}

void Yeti::requestSystemShutdown(const AmArg& args, AmArg& ret){
	graceful_suicide();
	ret.push(200);
	ret.push("OK");

}

void Yeti::requestSystemShutdownImmediate(const AmArg& args, AmArg& ret){
	immediate_suicide();
	ret.push(200);
	ret.push("OK");
}

void Yeti::requestSystemShutdownGraceful(const AmArg& args, AmArg& ret){
	set_system_shutdown(true);
	ret.push(200);
	ret.push("OK");
}

void Yeti::requestSystemShutdownCancel(const AmArg& args, AmArg& ret){
	set_system_shutdown(false);
	ret.push(200);
	ret.push("OK");
}
