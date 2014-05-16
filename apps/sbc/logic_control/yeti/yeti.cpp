#include "yeti.h"
#include "sdp_filter.h"

#include <string.h>
#include <ctime>

#include "log.h"
#include "AmPlugIn.h"
#include "AmArg.h"
#include "AmSession.h"
#include "AmUtils.h"
#include "AmAudioFile.h"
#include "AmMediaProcessor.h"
#include "SDPFilter.h"
#include "CallLeg.h"
#include "Version.h"
#include "RegisterDialog.h"
#include "Registration.h"
#include "SBC.h"
struct CallLegCreator;

class YetiFactory : public AmDynInvokeFactory
{
public:
	YetiFactory(const string& name)
		: AmDynInvokeFactory(name) {}

	~YetiFactory(){
		//DBG("~YetiFactory()");
		delete Yeti::instance();
	}

	AmDynInvoke* getInstance(){
		return Yeti::instance();
	}

	int onLoad(){
		if (Yeti::instance()->onLoad())
			return -1;
		DBG("logic control loaded.\n");
		return 0;
	}

};

EXPORT_PLUGIN_CLASS_FACTORY(YetiFactory, MOD_NAME);

Yeti* Yeti::_instance=0;

Yeti* Yeti::instance() {
	if(!_instance)
		_instance = new Yeti();
	return _instance;
}

Yeti::Yeti():
	router(new SqlRouter())
{
	routers.insert(router);
	//DBG("Yeti()");
}


Yeti::~Yeti() {
	//DBG("~Yeti()");
	router->release(routers);
	Registration::instance()->stop();
	rctl.stop();
}

bool Yeti::read_config(){
	if(cfg.loadFile(AmConfig::ModConfigPath + string(MOD_NAME ".conf"))) {
		ERROR("No configuration for "MOD_NAME" present (%s)\n",
			(AmConfig::ModConfigPath + string(MOD_NAME ".conf")).c_str());
		return false;
	}

	config.node_id = cfg.getParameterInt("node_id",-1);
	if(-1 == config.node_id){
		ERROR("Missed parameter 'node_id'");
		return false;
	}

	config.pop_id = cfg.getParameterInt("pop_id",-1);
	if(-1 == config.pop_id){
		ERROR("Missed parameter 'pop_id'");
		return false;
	}

	if(!cfg.hasParameter("routing_schema")){
		ERROR("Missed parameter 'routing_schema'");
		return false;
	}
	config.routing_schema = cfg.getParameter("routing_schema");

	if(!cfg.hasParameter("msg_logger_dir")){
		ERROR("Missed parameter 'msg_logger_dir'");
		return false;
	}
	config.msg_logger_dir = cfg.getParameter("msg_logger_dir");

	//check permissions for logger dir
	ofstream st;
	string testfile = config.msg_logger_dir + "/test";
	st.open(testfile.c_str(),std::ofstream::out | std::ofstream::trunc);
	if(!st.is_open()){
		ERROR("can't write test file in '%s' directory",config.msg_logger_dir.c_str());
		return false;
	}
	remove(testfile.c_str());


	return true;
}

int Yeti::onLoad() {
	if(!read_config()){
		return -1;
	}

	calls_show_limit = cfg.getParameterInt("calls_show_limit",100);

	self_iface.cc_module = "yeti";
	self_iface.cc_name = "yeti";
	self_iface.cc_values.clear();

	profile = new SBCCallProfile();
	string profile_file_name = AmConfig::ModConfigPath + "oodprofile.yeti.conf";
	if(!profile->readFromConfiguration("transparent",profile_file_name)){
		ERROR("can't read profile for OoD requests '%s'",profile_file_name.c_str());
		return -1;
	}
	profile->cc_vars.clear();
	profile->cc_interfaces.clear();

	DBG("p = %p",profile);
	if(rctl.configure(cfg)){
		ERROR("ResourceControl configure failed");
		return -1;
	}
	rctl.start();

	if(CodecsGroups::instance()->configure(cfg)){
		ERROR("CodecsGroups configure failed");
		return -1;
	}

	if (CodesTranslator::instance()->configure(cfg)){
		ERROR("CodesTranslator configure failed");
		return -1;
	}

	if (router->configure(cfg)){
		ERROR("SqlRouter confgiure failed");
		return -1;
	}

	if(router->run()){
		ERROR("SqlRouter start failed");
		return -1;
	}

	if(Registration::instance()->configure(cfg)){
		ERROR("Registration agent configure failed");
		return -1;
	}
	Registration::instance()->start();

	start_time = time(NULL);

	init_xmlrpc_cmds();

	return 0;
}

void Yeti::replace(string& s, const string& from, const string& to){
	size_t pos = 0;
	while ((pos = s.find(from, pos)) != string::npos) {
		s.replace(pos, from.length(), to);
		pos += s.length();
	}
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


SBCCallProfile& Yeti::getCallProfile(	const AmSipRequest& req,
										ParamReplacerCtx& ctx )
{
	return *profile;
}

SBCCallLeg *Yeti::getCallLeg(	const AmSipRequest& req,
								ParamReplacerCtx& ctx,
								CallLegCreator *leg_creator )
{
	DBG("%s()",FUNC_NAME);
	router_mutex.lock();
	SqlRouter *r = router;
	router_mutex.unlock();
	CallCtx *call_ctx = new CallCtx(r);

	PROF_START(gprof);

	r->getprofiles(req,*call_ctx);

	SqlCallProfile *profile = call_ctx->getFirstProfile();
	if(NULL == profile){
		r->release(routers);
		delete call_ctx;
		throw AmSession::Exception(500, SIP_REPLY_SERVER_INTERNAL_ERROR);
	}

	PROF_END(gprof);
	PROF_PRINT("getting profile",gprof);

	Cdr *cdr = call_ctx->cdr;

	ctx.call_profile = profile;
	if(check_and_refuse(profile,cdr,req,ctx,true)) {
		if(!call_ctx->SQLexception)	//avoid to write cdr on failed getprofile()
			r->write_cdr(cdr,true);
		r->release(routers);
		delete call_ctx;
		return NULL;
	}

	SBCCallProfile& call_profile = *profile;

	profile->cc_interfaces.push_back(self_iface);

	SBCCallLeg* b2b_dlg = leg_creator->create(call_profile);

	b2b_dlg->setLogicData(reinterpret_cast<void *>(call_ctx));

	return b2b_dlg;
}
