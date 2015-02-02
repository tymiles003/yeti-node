#include "yeti.h"
#include "Version.h"
#include "Registration.h"
#include "codecs_bench.h"
#include "alarms.h"

#include "sip/resolver.h"
#include "sip/transport.h"
#include "AmPlugIn.h"

#include <sys/types.h>
#include <signal.h>

#include "XmlRpcException.h"

const char *XMLRPC_CMD_SUCC = "OK";

static timeval last_shutdown_time;

typedef void (Yeti::*YetiRpcHandler)(const AmArg& args, AmArg& ret);

#define handler_log() DBG("execute handler: %s(%s)",FUNC_NAME,args.print(args).c_str());

class CdrNotFoundException: public XmlRpc::XmlRpcException {
  public:
	CdrNotFoundException(string local_tag):
		XmlRpc::XmlRpcException("call with local_tag: '"+local_tag+"' is not found",404) {}
};

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

	timerclear(&last_shutdown_time);

	/* show */
	reg_leaf(root,show,"show","read only queries");

		reg_method(show,"version","show version",showVersion,"");

		reg_leaf(show,show_resource,"resource","resources related functions");

			reg_leaf_method_arg(show_resource,show_resource_state,"state","get resources state from redis",getResourceState,
								"","<type>/-1 <id>/-1","retreive info about certain resources state");

			reg_leaf_method(show_resource_state,show_resource_state_used,"used","show active resources handlers",showResources,"");
			reg_method_arg(show_resource_state_used,"handler","find resource by handler id",showResourceByHandler,"",
						   "<handler_id>","find resource by handler id");
			reg_method_arg(show_resource_state_used,"owner_tag","find resource by onwer local_tag",showResourceByLocalTag,"",
						   "<onwer_local_tag>","find resource by onwer local_tag");
			reg_method_arg(show_resource_state_used,"resource_id","find handlers which manage resources with ceration id",showResourcesById,"",
						   "<resource_id>","find handlers which manage resources with ceration id");


			reg_method(show_resource,"types","show resources types",showResourceTypes,"");

		reg_method(show,"sensors","show active sensors configuration",showSensorsState,"");
		/*reg_leaf(show,show_sensors,"sensors","sensors related functions");
			reg_method(show_sensors,"state","show active sensors configuration",showSensorsState,"");*/

		reg_leaf(show,show_router,"router","active router instance");
			reg_method(show_router,"cache","show callprofile's cache state",ShowCache,"");

			reg_leaf(show_router,show_router_cdrwriter,"cdrwriter","cdrwriter");
				reg_method(show_router_cdrwriter,"opened-files","show opened csv files",showRouterCdrWriterOpenedFiles,"");

		reg_leaf(show,show_media,"media","media processor instance");
			reg_method(show_media,"streams","active media streams info",showMediaStreams,"");

		reg_leaf_method_arg(show,show_calls,"calls","active calls",GetCalls,"show current active calls",
						"<LOCAL-TAG>","retreive call by local_tag");
			reg_method(show_calls,"count","active calls count",GetCallsCount,"");
			reg_method(show_calls,"fields","show available call fields",showCallsFields,"");
			reg_method_arg(show_calls,"filtered","active calls. specify desired fields",GetCallsFields,"",
						"<field1> <field2> ...","active calls. send only certain fields");

		reg_method(show,"configuration","actual settings",GetConfig,"");

		reg_method(show,"stats","runtime statistics",GetStats,"");

		reg_method(show,"interfaces","show network interfaces configuration",showInterfaces,"");

		reg_leaf_method_arg(show,show_registrations,"registrations","uac registrations",GetRegistrations,"show configured uac registrations",
							"<id>","get registration by id");
			reg_method(show_registrations,"count","active registrations count",GetRegistrationsCount,"");

		reg_leaf(show,show_system,"system","system cmds");
			reg_method(show_system,"log-level","loglevels",showSystemLogLevel,"");
			reg_method(show_system,"status","system states",showSystemStatus,"");
			reg_method(show_system,"alarms","system alarms",showSystemAlarms,"");
			reg_method(show_system,"session-limit","actual sessions limit config",showSessions,"");

	/* request */
	reg_leaf(root,request,"request","modify commands");

		reg_leaf(request,request_sensors,"sensors","sensors");
			reg_method(request_sensors,"reload","reload sensors",requestReloadSensors,"");

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

		reg_leaf(request,request_resource,"resource","resources cache");
			/*reg_method_arg(request_resource,"state","",getResourceState,
						   "","<type> <id>","get current state of resource");*/
			reg_method(request_resource,"invalidate","invalidate all resources",requestResourcesInvalidate,"");

		reg_leaf(request,request_resolver,"resolver","dns resolver instance");
			reg_method(request_resolver,"clear","clear dns cache",requestResolverClear,"");
			reg_method_arg(request_resolver,"get","",requestResolverGet,
						   "","<name>","resolve dns name");
	/* set */
	reg_leaf(root,lset,"set","set");
		reg_leaf(lset,set_system,"system","system commands");
			reg_leaf(set_system,set_system_log_level,"log-level","logging facilities level");
				reg_method_arg(set_system_log_level,"di_log","",setSystemLogDiLogLevel,
							   "","<log_level>","set new log level");
				reg_method_arg(set_system_log_level,"syslog","",setSystemLogSyslogLevel,
							   "","<log_level>","set new log level");

			reg_method_arg(set_system,"session-limit","",setSessionsLimit,
						   "","<limit> <overload response code> <overload response reason>","set new session limit params");

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
				if(!e->hasLeafs()&&e->arg.empty())
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
			throw XmlRpc::XmlRpcException("no such event_id",503);
		}
	} catch(pqxx::pqxx_exception &e){
		DBG("e = %s",e.base().what());
		throw XmlRpc::XmlRpcException(string("can't check event id in database ")+e.base().what(),500);
	} catch(XmlRpc::XmlRpcException){
		throw;
	} catch(...){
		throw XmlRpc::XmlRpcException("can't check event id in database",500);
	}
	return succ;
}

bool Yeti::assert_event_id(const AmArg &args,AmArg &ret){
	if(args.size()){
		int event_id;
		args.assertArrayFmt("s");
		if(!str2int(args[0].asCStr(),event_id)){
			throw XmlRpc::XmlRpcException("invalid event id",500);
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
	handler_log();
	ret = (long int)cdr_list.get_count();
}

void Yeti::GetCall(const AmArg& args, AmArg& ret) {
	string local_tag;
	handler_log();

	if (!args.size()) {
		throw XmlRpc::XmlRpcException("Parameters error: expected local tag of requested cdr",500);
	}

	local_tag = args[0].asCStr();
	if(!cdr_list.getCall(local_tag,ret,router)){
		throw CdrNotFoundException(local_tag);
	}
}

void Yeti::GetCalls(const AmArg& args, AmArg& ret) {
	handler_log();
	if(args.size()){
		string local_tag = args[0].asCStr();
		if(!cdr_list.getCall(local_tag,ret,router)){
			throw CdrNotFoundException(local_tag);
		}
	} else {
		cdr_list.getCalls(ret,calls_show_limit,router);
	}
}

void Yeti::GetCallsFields(const AmArg &args, AmArg &ret){
	handler_log();

	if(!args.size()){
		throw XmlRpc::XmlRpcException("you should specify at least one field",500);
	}

	AmArg failed_fields;
	vector<string> str_fields = args.asStringVector();
	if(!cdr_list.validate_fields(str_fields,router,failed_fields)){
		ERROR("GetCallsFiltered: passed non existent fields: %s",AmArg::print(failed_fields).c_str());
		throw XmlRpc::XmlRpcException("passed one or more non existent fields",500);
	}

	cdr_list.getCallsFields(ret,calls_show_limit,router,str_fields);
}

void Yeti::showCallsFields(const AmArg &args, AmArg &ret){
	cdr_list.getFields(ret,router);
}

void Yeti::GetRegistration(const AmArg& args, AmArg& ret){
	string reg_id_str;
	int reg_id;
	handler_log();
	if (!args.size()) {
		throw XmlRpc::XmlRpcException("Parameters error: expected id of requested registration",500);
	}

	reg_id_str = args[0].asCStr();
	if(!str2int(reg_id_str,reg_id)){
		throw XmlRpc::XmlRpcException("Non integer value passed as registrations id",500);
	}

	if(!Registration::instance()->get_registration_info(reg_id,ret)){
		throw XmlRpc::XmlRpcException("Have no registration with such id",404);
	}
}

void Yeti::RenewRegistration(const AmArg& args, AmArg& ret){
	string reg_id_str;
	int reg_id;
	handler_log();
	if (!args.size()) {
		throw XmlRpc::XmlRpcException("Parameters error: expected id of active registration",500);
	}

	reg_id_str = args[0].asCStr();
	if(!str2int(reg_id_str,reg_id)){
		throw XmlRpc::XmlRpcException("Non integer value passed as registrations id",500);
	}

	if(!Registration::instance()->reregister(reg_id)){
		throw XmlRpc::XmlRpcException("Have no registration with such id and in appropriate state",404);
	}
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::GetRegistrations(const AmArg& args, AmArg& ret){
	handler_log();
	if(args.size()){
		GetRegistration(args,ret);
		return;
	}
	Registration::instance()->list_registrations(ret);
}

void Yeti::GetRegistrationsCount(const AmArg& args, AmArg& ret){
	handler_log();
	ret = Registration::instance()->get_registrations_count();
}

void Yeti::ClearStats(const AmArg& args, AmArg& ret){
	handler_log();
	AmLock l(router_mutex);
	if(router)
		router->clearStats();
	rctl.clearStats();
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::ClearCache(const AmArg& args, AmArg& ret){
	handler_log();
	AmLock l(router_mutex);
	if(router)
		router->clearCache();
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::ShowCache(const AmArg& args, AmArg& ret){
	handler_log();
	AmLock l(router_mutex);
	if(router)
		router->showCache(ret);
}

void Yeti::GetStats(const AmArg& args, AmArg& ret){
	time_t now;
	handler_log();

	/* Yeti stats */
	ret["calls_show_limit"] = (int)calls_show_limit;
	now = time(NULL);
	ret["localtime"] = now;
	ret["uptime"] = difftime(now,start_time);

	/* sql_router stats */
	router_mutex.lock();
	AmArg &routers_stats = ret["routers"];
	ret["active_routers_count"] = (long int)routers.size();
	set<SqlRouter *>::const_iterator i = routers.begin();
	for(;i!=routers.end();++i){
		SqlRouter *r = *i;
		if(r){
			routers_stats.push(AmArg());
			AmArg &rs = routers_stats.back();
			r->getStats(rs);
			rs["is_active"] = (r == router);
		}
	}
	router_mutex.unlock();

	AmSessionContainer::instance()->getStats(ret["AmSessionContainer"]);

	AmArg &ss = ret["AmSession"];
	ss["SessionNum"] = (int)AmSession::getSessionNum();
	ss["MaxSessionNum"] = (int)AmSession::getMaxSessionNum();
	ss["AvgSessionNum"] = (int)AmSession::getAvgSessionNum();

	AmArg &ts = ret["trans_layer"];
	const trans_stats &tstats = trans_layer::instance()->get_stats();
	ts["rx_replies"] = (long)tstats.get_received_replies();
	ts["tx_replies"] = (long)tstats.get_sent_replies();
	ts["tx_replies_retrans"] = (long)tstats.get_sent_reply_retrans();
	ts["rx_requests"] =(long) tstats.get_received_requests();
	ts["tx_requests"] = (long)tstats.get_sent_requests();
	ts["tx_requests_retrans"] = (long)tstats.get_sent_request_retrans();

	rctl.getStats(ret["resource_control"]);
	CodesTranslator::instance()->getStats(ret["translator"]);
}

void Yeti::GetConfig(const AmArg& args, AmArg& ret) {
	handler_log();

	ret["calls_show_limit"] = calls_show_limit;
	ret["node_id"] = config.node_id;
	ret["pop_id"] = config.pop_id;

	router_mutex.lock();
	if(router){
		router->getConfig(ret["router"]);
	}
	router_mutex.unlock();

	CodesTranslator::instance()->GetConfig(ret["translator"]);
	rctl.GetConfig(ret["resources_control"]);
	CodecsGroups::instance()->GetConfig(ret["codecs_groups"]);
}

void Yeti::DropCall(const AmArg& args, AmArg& ret){
	SBCControlEvent* evt;
	string local_tag;
	handler_log();

	if (!args.size()){
		throw XmlRpc::XmlRpcException("Parameters error: expected local tag of active call",500);
	}

	local_tag = args[0].asCStr();

	evt = new SBCControlEvent("teardown");

	if (!AmSessionContainer::instance()->postEvent(local_tag, evt)) {
		/* hack: if cdr not in AmSessionContainer but present in cdr_list then drop it and write cdr */
		cdr_list.lock();
			Cdr *cdr = cdr_list.get_by_local_tag(local_tag);
			if(cdr){
				//don't check for inserted2list. we just got it from here.
				cdr_list.erase_unsafe(local_tag,false);
			}
		cdr_list.unlock();
		if(cdr){
			ERROR("Yeti::DropCall() call %s not in AmSessionContainer but in CdrList. "
				  "remove it from CdrList and write CDR using active router instance",local_tag.c_str());
			router_mutex.lock(); //avoid unexpected router reload
				router->write_cdr(cdr,true);
			router_mutex.unlock();
			ret = "Dropped from active_calls (no presented in sessions container)";
		} else {
			throw CdrNotFoundException(local_tag);
		}
	} else {
		ret = "Dropped from sessions container";
	}
}

void Yeti::showVersion(const AmArg& args, AmArg& ret) {
	handler_log();
	ret["build"] = YETI_VERSION;
	ret["build_commit"] = YETI_COMMIT;
	ret["compiled_at"] = YETI_BUILD_DATE;
	ret["compiled_by"] = YETI_BUILD_USER;
}

void Yeti::reloadResources(const AmArg& args, AmArg& ret){
	handler_log();
	if(!assert_event_id(args,ret))
		return;
	rctl.configure_db(cfg);
	if(!rctl.reload()){
		throw XmlRpc::XmlRpcException("errors during resources config reload. leave old state",500);
	}
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::reloadTranslations(const AmArg& args, AmArg& ret){
	handler_log();
	if(!assert_event_id(args,ret))
		return;
	CodesTranslator::instance()->configure_db(cfg);
	if(!CodesTranslator::instance()->reload()){
		throw XmlRpc::XmlRpcException("errors during translations config reload. leave old state",500);
	}
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::reloadRegistrations(const AmArg& args, AmArg& ret){
	handler_log();
	if(!assert_event_id(args,ret))
		return;
	if(0==Registration::instance()->reload(cfg)){
		ret = XMLRPC_CMD_SUCC;
	} else {
		throw XmlRpc::XmlRpcException("errors during registrations config reload. leave old state",500);
	}
}

void Yeti::reloadCodecsGroups(const AmArg& args, AmArg& ret){
	handler_log();
	if(!assert_event_id(args,ret))
		return;
	CodecsGroups::instance()->configure_db(cfg);
	if(!CodecsGroups::instance()->reload()){
		throw XmlRpc::XmlRpcException("errors during codecs groups reload. leave old state",500);
	}
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::reloadRouter(const AmArg& args, AmArg& ret){
	handler_log();
	if(!assert_event_id(args,ret))
		return;
	INFO("Allocate new SqlRouter instance");
	SqlRouter *r = new SqlRouter();

	INFO("Configure SqlRouter");
	if (r->configure(cfg)){
		ERROR("SqlRouter confgiure failed");
		delete r;
		throw XmlRpc::XmlRpcException("SqlRouter confgiure failed",500);
	}

	INFO("Run SqlRouter");
	if(r->run()){
		ERROR("SqlRouter start failed");
		delete r;
		throw XmlRpc::XmlRpcException("SqlRouter start failed",500);
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
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::requestReloadSensors(const AmArg& args, AmArg& ret){
	handler_log();
	if(!assert_event_id(args,ret))
		return;
	Sensors::instance()->configure_db(cfg);
	if(Sensors::instance()->reload()){
		ret = XMLRPC_CMD_SUCC;
	} else {
		throw XmlRpc::XmlRpcException("errors during sensors reload. leave old state",500);
	}
}

void Yeti::showSensorsState(const AmArg& args, AmArg& ret){
	handler_log();
	Sensors::instance()->GetConfig(ret);
}

void Yeti::closeCdrFiles(const AmArg& args, AmArg& ret){
	handler_log();
	router_mutex.lock();
		set<SqlRouter *>::const_iterator i = routers.begin();
		for(;i!=routers.end();++i){
			if(*i) (*i)->closeCdrFiles();
		}
	router_mutex.unlock();
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::showMediaStreams(const AmArg& args, AmArg& ret){
	handler_log();
	AmMediaProcessor::instance()->getInfo(ret);
}

void Yeti::showPayloads(const AmArg& args, AmArg& ret){
	vector<SdpPayload> payloads;
	unsigned char *buf;
	int size = 0;
	handler_log();
	bool compute_cost = args.size() && args[0] == "benchmark";
	string path = args.size()>1 ? args[1].asCStr() : DEFAULT_BECH_FILE_PATH;

	const AmPlugIn* plugin = AmPlugIn::instance();
	plugin->getPayloads(payloads);

	if(compute_cost){
		size = load_testing_source(path,buf);
		compute_cost = size > 0;
	}

	vector<SdpPayload>::const_iterator it = payloads.begin();
	for(;it!=payloads.end();++it){
		const SdpPayload &p = *it;
		ret.push(p.encoding_name,AmArg());
		AmArg &a = ret[p.encoding_name];

		DBG("process codec: %s (%d)",
			p.encoding_name.c_str(),p.payload_type);
		a["payload_type"] = p.payload_type;
		a["clock_rate"] = p.clock_rate;
		if(compute_cost){
			get_codec_cost(p.payload_type,buf,size,a);
		}
	}

	if(compute_cost)
		delete[] buf;
}

void Yeti::showInterfaces(const AmArg& args, AmArg& ret){
	handler_log();

	AmArg &sig = ret["sip"];
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

	AmArg &sip_map = ret["sip_map"];
	for(multimap<string,unsigned short>::iterator it = AmConfig::LocalSIPIP2If.begin();
		it != AmConfig::LocalSIPIP2If.end(); ++it) {
		AmConfig::SIP_interface& iface = AmConfig::SIP_Ifs[it->second];
		sip_map[it->first] = iface.name.empty() ? "default" : iface.name;
	}

	AmArg &rtp = ret["media"];
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
}

void Yeti::showRouterCdrWriterOpenedFiles(const AmArg& args, AmArg& ret){
	handler_log();
	router_mutex.lock();
		set<SqlRouter *>::const_iterator i = routers.begin();
		for(;i!=routers.end();++i){
			if(*i) {
				AmArg a;
				ostringstream ss;
				ss << hex << *i;
				(*i)->showOpenedFiles(a);
				ret[ss.str()] = a;
			}
		}
	router_mutex.unlock();
}

void Yeti::requestSystemLogDump(const AmArg& args, AmArg& ret){
	handler_log();

	if(!args.size()){
		throw XmlRpc::XmlRpcException("missed path for dump",500);
	}

	AmDynInvokeFactory* di_log = AmPlugIn::instance()->getFactory4Di("di_log");
	if(0==di_log){
		throw XmlRpc::XmlRpcException("di_log module not loaded",404);
	}

	AmArg s;
	di_log->getInstance()->invoke("dumplogtodisk",args,s);
	ret = XMLRPC_CMD_SUCC;
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
		throw XmlRpc::XmlRpcException("missed new log_level",500);
	}
	args.assertArrayFmt("s");
	if(!str2int(args.get(0).asCStr(),log_level)){
		throw XmlRpc::XmlRpcException("invalid log_level fmt",500);
	}

	AmLoggingFacility* fac = AmPlugIn::instance()->getFactory4LogFaclty(facility_name);
	if(0==fac){
		throw XmlRpc::XmlRpcException("logging facility not loaded",404);
	}

	fac->setLogLevel(log_level);

	ret = XMLRPC_CMD_SUCC;
}

void Yeti::showSystemLogLevel(const AmArg& args, AmArg& ret){
	handler_log();
	ret["log_level"] = log_level;
	addLoggingFacilityLogLevel(ret["facilities"],"syslog");
	addLoggingFacilityLogLevel(ret["facilities"],"di_log");
}

void Yeti::setSystemLogSyslogLevel(const AmArg& args, AmArg& ret){
	handler_log();
	setLoggingFacilityLogLevel(args,ret,"syslog");
}

void Yeti::setSystemLogDiLogLevel(const AmArg& args, AmArg& ret){
	handler_log();
	setLoggingFacilityLogLevel(args,ret,"di_log");
}

void Yeti::showSystemStatus(const AmArg& args, AmArg& ret){
	handler_log();
	ret["shutdown_mode"] = (bool)AmConfig::ShutdownMode;
	ret["shutdown_request_time"] = !timerisset(&last_shutdown_time) ?
					"nil" : timeval2str(last_shutdown_time);
}

void Yeti::showSystemAlarms(const AmArg& args, AmArg& ret){
	handler_log();
	alarms *a = alarms::instance();
	for(int id = 0; id < alarms::MAX_ALARMS; id++){
		ret.push(AmArg());
		a->get(id).getInfo(ret.back());
	}
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
	handler_log();
	graceful_suicide();
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::requestSystemShutdownImmediate(const AmArg& args, AmArg& ret){
	handler_log();
	immediate_suicide();
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::requestSystemShutdownGraceful(const AmArg& args, AmArg& ret){
	handler_log();
	gettimeofday(&last_shutdown_time,NULL);
	set_system_shutdown(true);
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::requestSystemShutdownCancel(const AmArg& args, AmArg& ret){
	handler_log();
	timerclear(&last_shutdown_time);
	set_system_shutdown(false);
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::getResourceState(const AmArg& args, AmArg& ret){
	handler_log();
	int type, id;

	if(args.size()<2){
		throw XmlRpc::XmlRpcException("specify type and id of resource",500);
	}
	args.assertArrayFmt("ss");
	if(!str2int(args.get(0).asCStr(),type)){
		throw XmlRpc::XmlRpcException("invalid resource type",500);
	}
	if(!str2int(args.get(1).asCStr(),id)){
		throw XmlRpc::XmlRpcException("invalid resource id",500);
	}

	try {
		rctl.getResourceState(type,id,ret);
	} catch(const ResourceCacheException &e){
		throw XmlRpc::XmlRpcException(e.what,e.code);
	}
}

void Yeti::showResources(const AmArg& args, AmArg& ret){
	handler_log();
	rctl.showResources(ret);
}

void Yeti::showResourceByHandler(const AmArg& args, AmArg& ret){
	handler_log();
	if(!args.size()){
		throw XmlRpc::XmlRpcException("specify handler id",500);
	}
	rctl.showResourceByHandler(args.get(0).asCStr(),ret);
}

void Yeti::showResourceByLocalTag(const AmArg& args, AmArg& ret){
	handler_log();
	if(!args.size()){
		throw XmlRpc::XmlRpcException("specify local_tag",500);
	}
	rctl.showResourceByLocalTag(args.get(0).asCStr(),ret);
}

void Yeti::showResourcesById(const AmArg& args, AmArg& ret){
	handler_log();

	int id;
	if(!args.size()){
		throw XmlRpc::XmlRpcException("specify resource id",500);
	}
	if(!str2int(args.get(0).asCStr(),id)){
		throw XmlRpc::XmlRpcException("invalid resource id",500);
	}
	rctl.showResourcesById(id,ret);
}

void Yeti::showResourceTypes(const AmArg& args, AmArg& ret){
	handler_log();
	rctl.GetConfig(ret,true);
}

void Yeti::requestResourcesInvalidate(const AmArg& args, AmArg& ret){
	handler_log();
	if(rctl.invalidate_resources()){
		ret = XMLRPC_CMD_SUCC;
	} else {
		throw XmlRpc::XmlRpcException("handlers invalidated. but resources initialization failed",500);
	}
}

void Yeti::showSessions(const AmArg& args, AmArg& ret){
	handler_log();

	ret["limit"] = (long int)AmConfig::SessionLimit;
	ret["limit_error_code"] = (long int)AmConfig::SessionLimitErrCode;
	ret["limit_error_reason"] = AmConfig::SessionLimitErrReason;
}

void Yeti::setSessionsLimit(const AmArg& args, AmArg& ret){
	handler_log();
	if(args.size()<3){
		throw XmlRpc::XmlRpcException("missed parameter",500);
	}
	args.assertArrayFmt("sss");

	int limit,code;
	if(!str2int(args.get(0).asCStr(),limit)){
		throw XmlRpc::XmlRpcException("non integer value for sessions limit",500);
	}
	if(!str2int(args.get(1).asCStr(),code)){
		throw XmlRpc::XmlRpcException("non integer value for overload response code",500);
	}

	AmConfig::SessionLimit = limit;
	AmConfig::SessionLimitErrCode = code;
	AmConfig::SessionLimitErrReason = args.get(2).asCStr();

	ret = XMLRPC_CMD_SUCC;
}


void Yeti::requestResolverClear(const AmArg& args, AmArg& ret){
	handler_log();
	resolver::instance()->clear_cache();
	ret = XMLRPC_CMD_SUCC;
}

void Yeti::requestResolverGet(const AmArg& args, AmArg& ret){
	handler_log();
	if(!args.size()){
		throw XmlRpc::XmlRpcException("missed parameter",500);
	}
	sockaddr_storage ss;
	dns_handle dh;
	int err = resolver::instance()->resolve_name(args.get(0).asCStr(),&dh,&ss,IPv4);
	if(err == -1){
		throw XmlRpc::XmlRpcException("can't resolve",500);
	}
	ret["address"] = get_addr_str(&ss).c_str();
	dh.dump(ret["handler"]);
}

