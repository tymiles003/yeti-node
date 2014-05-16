#include "yeti.h"
#include "sdp_filter.h"
#include "codecs_bench.h"

#include <string.h>
#include <ctime>

#include "log.h"
#include "AmPlugIn.h"
#include "AmArg.h"
#include "AmSession.h"
#include "AmUtils.h"
#include "AmAudioFile.h"
#include "AmMediaProcessor.h"
#include "sip/transport.h"
#include "SDPFilter.h"
#include "CallLeg.h"
#include "Version.h"
#include "RegisterDialog.h"
#include "Registration.h"
#include "SBC.h"
struct CallLegCreator;

#if YETI_ENABLE_PROFILING

#define PROF_START(var) clock_t prof_start_##var = clock();
#define PROF_END(var) clock_t prof_end_##var = clock();
#define PROF_DIFF(var) ((prof_end_##var-prof_start_##var)/(double)CLOCKS_PER_SEC)
#define PROF_PRINT(descr,var) DBG(descr" took %f",PROF_DIFF(var));

#else

#define PROF_START(var) ;
#define PROF_END(var) ;
#define PROF_DIFF(var) (-1)
#define PROF_PRINT(descr,var) ;


#endif

#define getCtx_void()\
	CallCtx *ctx = getCtx(call);\
	if(NULL==ctx){\
		ERROR("CallCtx = nullptr ");\
		log_stacktrace(L_ERR);\
		return;\
	}

#define getCtx_chained()\
	CallCtx *ctx = getCtx(call);\
	if(NULL==ctx){\
		ERROR("CallCtx = nullptr ");\
		log_stacktrace(L_ERR);\
		return ContinueProcessing;\
	}


//CallCtx *getCtx(SBCCallLeg *call){ return reinterpret_cast<CallCtx *>(call->getLogicData()); }
Cdr *getCdr(CallCtx *ctx) { return ctx->cdr; }
Cdr *getCdr(SBCCallLeg *call) { return getCdr(getCtx(call)); }

static const char *callStatus2str(const CallLeg::CallStatus state)
{
	static const char *disconnected = "Disconnected";
	static const char *disconnecting = "Disconnecting";
	static const char *noreply = "NoReply";
	static const char *ringing = "Ringing";
	static const char *connected = "Connected";
	static const char *unknown = "???";

	switch (state) {
		case CallLeg::Disconnected: return disconnected;
		case CallLeg::Disconnecting: return disconnecting;
		case CallLeg::NoReply: return noreply;
		case CallLeg::Ringing: return ringing;
		case CallLeg::Connected: return connected;
	}

	return unknown;
}

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

	Cdr *cdr = getCdr(call_ctx);

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

bool Yeti::connectCallee(SBCCallLeg *call,const AmSipRequest &orig_req){

	SBCCallProfile &call_profile = call->getCallProfile();
	ParamReplacerCtx ctx(&call_profile);
	ctx.app_param = getHeader(orig_req.hdrs, PARAM_HDR, true);

	AmSipRequest uac_req(orig_req);
	AmUriParser uac_ruri;

	uac_ruri.uri = uac_req.r_uri;
	if(!uac_ruri.parse_uri()) {
		DBG("Error parsing R-URI '%s'\n",uac_ruri.uri.c_str());
		throw AmSession::Exception(400,"Failed to parse R-URI");
	}

	call_profile.sst_aleg_enabled = ctx.replaceParameters(
		call_profile.sst_aleg_enabled,
		"enable_aleg_session_timer",
		orig_req
	);

	call_profile.sst_enabled = ctx.replaceParameters(
		call_profile.sst_enabled,
		"enable_session_timer", orig_req
	);

	if ((call_profile.sst_aleg_enabled == "yes") ||
		(call_profile.sst_enabled == "yes"))
	{
		call_profile.eval_sst_config(ctx,orig_req,call_profile.sst_a_cfg);
		if(call->applySSTCfg(call_profile.sst_a_cfg,&orig_req) < 0) {
			throw AmSession::Exception(500, SIP_REPLY_SERVER_INTERNAL_ERROR);
		}
	}


	if (!call_profile.evaluate(ctx, orig_req)) {
		ERROR("call profile evaluation failed\n");
		throw AmSession::Exception(500, SIP_REPLY_SERVER_INTERNAL_ERROR);
	}

	if(call_profile.contact_hiding) {
		if(RegisterDialog::decodeUsername(orig_req.user,uac_ruri)) {
			uac_req.r_uri = uac_ruri.uri_str();
		}
	} else if(call_profile.reg_caching) {
		// REG-Cache lookup
		uac_req.r_uri = call_profile.retarget(orig_req.user,*call->dlg);
	}

	string ruri, to, from;

	ruri = call_profile.ruri.empty() ? uac_req.r_uri : call_profile.ruri;
	if(!call_profile.ruri_host.empty()){
		ctx.ruri_parser.uri = ruri;
		if(!ctx.ruri_parser.parse_uri()) {
			WARN("Error parsing R-URI '%s'\n", ruri.c_str());
		} else {
			ctx.ruri_parser.uri_port.clear();
			ctx.ruri_parser.uri_host = call_profile.ruri_host;
			ruri = ctx.ruri_parser.uri_str();
		}
	}
	from = call_profile.from.empty() ? orig_req.from : call_profile.from;
	to = call_profile.to.empty() ? orig_req.to : call_profile.to;

	call->applyAProfile();
	call_profile.apply_a_routing(ctx,orig_req,*call->dlg);

	AmSipRequest invite_req(orig_req);

	removeHeader(invite_req.hdrs,PARAM_HDR);
	removeHeader(invite_req.hdrs,"P-App-Name");

	if (call_profile.sst_enabled_value) {
		removeHeader(invite_req.hdrs,SIP_HDR_SESSION_EXPIRES);
		removeHeader(invite_req.hdrs,SIP_HDR_MIN_SE);
	}

	size_t start_pos = 0;
	while (start_pos<call_profile.append_headers.length()) {
		int res;
		size_t name_end, val_begin, val_end, hdr_end;
		if ((res = skip_header(call_profile.append_headers, start_pos, name_end, val_begin,
				val_end, hdr_end)) != 0) {
			ERROR("skip_header for '%s' pos: %ld, return %d",
					call_profile.append_headers.c_str(),start_pos,res);
			throw AmSession::Exception(500, SIP_REPLY_SERVER_INTERNAL_ERROR);
		}
		string hdr_name = call_profile.append_headers.substr(start_pos, name_end-start_pos);
		while(!getHeader(invite_req.hdrs,hdr_name).empty()){
			removeHeader(invite_req.hdrs,hdr_name);
		}
		start_pos = hdr_end;
	}

	inplaceHeaderFilter(invite_req.hdrs, call_profile.headerfilter);

	if (call_profile.append_headers.size() > 2) {
		string append_headers = call_profile.append_headers;
		assertEndCRLF(append_headers);
		invite_req.hdrs+=append_headers;
	}

	int res = filterRequestSdp(call,
							   call_profile,
							   invite_req.body,invite_req.method,
							   call_profile.static_codecs_bleg_id);
	if(res < 0){
		INFO("onInitialInvite() Not acceptable codecs for legB");
		throw AmSession::Exception(488, SIP_REPLY_NOT_ACCEPTABLE_HERE);
	}

	call->connectCallee(to, ruri, from, orig_req, invite_req);

	return false;
}

bool Yeti::chooseNextProfile(SBCCallLeg *call){
	DBG("%s()",FUNC_NAME);

	string refuse_reason;
	int refuse_code;
	CallCtx *ctx;
	Cdr *cdr;
	SqlCallProfile *profile = NULL;
	ResourceCtlResponse rctl_ret;
	bool has_profile = false;

	ctx = getCtx(call);
	cdr = getCdr(ctx);

	profile = ctx->getNextProfile(false);

	if(NULL==profile){
		//pretend that nothing happen. we were never called
		DBG("%s() no more profiles or refuse profile on serial fork. ignore it",FUNC_NAME);
		return false;
	}

	//write cdr and replace ctx pointer with new
	cdr_list.erase(cdr);
	ctx->router->write_cdr(cdr,false);
	cdr = getCdr(ctx);

	do {
		if(NULL==profile){
			DBG("%s() there are no profiles more",FUNC_NAME);
			break;
		}

		DBG("%s() choosed next profile. check it for refuse",FUNC_NAME);

		ParamReplacerCtx rctx(profile);
		if(check_and_refuse(profile,cdr,*ctx->initial_invite,rctx)){
			DBG("%s() profile contains refuse code",FUNC_NAME);
			break;
		}

		DBG("%s() no refuse field. check it for resources",FUNC_NAME);

		rctl_ret = rctl.get(ctx->getCurrentResourceList(),refuse_code,refuse_reason);

		if(rctl_ret == RES_CTL_OK){
			DBG("%s() check resources  successed",FUNC_NAME);
			profile = ctx->getCurrentProfile();
			has_profile = true;
			break;
		} else if(	rctl_ret ==  RES_CTL_REJECT ||
					rctl_ret ==  RES_CTL_ERROR){
			DBG("%s() check resources failed with code: %d, reply: <%d '%s'>",FUNC_NAME,
				rctl_ret,refuse_code,refuse_reason.c_str());
			break;
		} else if(	rctl_ret == RES_CTL_NEXT){
			DBG("%s() check resources failed with code: %d, reply: <%d '%s'>",FUNC_NAME,
				rctl_ret,refuse_code,refuse_reason.c_str());

			profile = ctx->getNextProfile(false);
			//write old cdr here
			ctx->router->write_cdr(cdr,true);
			cdr = getCdr(ctx);
		}
	} while(rctl_ret != RES_CTL_OK);

	if(!has_profile){
		cdr->update_internal_reason(DisconnectByTS,refuse_reason,refuse_code);
		return false;
	} else {
		DBG("%s() update call profile for legA",FUNC_NAME);
		profile->cc_interfaces.push_back(self_iface);
		call->getCallProfile() = *profile;
		return true;
	}
}

bool Yeti::check_and_refuse(SqlCallProfile *profile,Cdr *cdr,
							const AmSipRequest& req,ParamReplacerCtx& ctx,
							bool send_reply){
	bool need_reply;
	bool write_cdr;
	unsigned int internal_code,response_code;
	string internal_reason,response_reason;

	if(profile->disconnect_code_id==0)
		return false;

	write_cdr = CodesTranslator::instance()->translate_db_code(profile->disconnect_code_id,
							 internal_code,internal_reason,
							 response_code,response_reason,
							 profile->aleg_override_id);
	need_reply = (response_code!=NO_REPLY_DISCONNECT_CODE);

	if(write_cdr){
		cdr->update(Start);
		cdr->update_internal_reason(DisconnectByDB,internal_reason,internal_code);
		cdr->update_aleg_reason(response_reason,response_code);
	} else {
		cdr->setSuppress(true);
	}
	if(send_reply && need_reply){
		if(write_cdr){
			cdr->update(req);
			cdr->update_sbc(*profile);
		}
		//prepare & send sip response
		string hdrs = ctx.replaceParameters(profile->append_headers, "append_headers", req);
		if (hdrs.size()>2)
			assertEndCRLF(hdrs);
		AmSipDialog::reply_error(req, response_code, response_reason, hdrs);
	}
	return true;
}

	//!InDialog handlers

void Yeti::start(const string& cc_name, const string& ltag,
				SBCCallProfile* call_profile,
				int start_ts_sec, int start_ts_usec,
				const AmArg& values, int timer_id, AmArg& res)
{
	DBG("%s(%p,%s)",FUNC_NAME,call_profile,ltag.c_str());
	res.push(AmArg());
}

void Yeti::connect(const string& cc_name, const string& ltag,
				SBCCallProfile* call_profile,
				const string& other_tag,
				int connect_ts_sec, int connect_ts_usec)
{
	DBG("%s(%p,%s)",FUNC_NAME,call_profile,ltag.c_str());
}

void Yeti::end(const string& cc_name, const string& ltag,
			SBCCallProfile* call_profile,
			int end_ts_sec, int end_ts_usec)
{
	DBG("%s(%p,%s)",FUNC_NAME,call_profile,ltag.c_str());
}

void Yeti::oodHandlingTerminated(const AmSipRequest *req,SqlCallProfile *call_profile){
	DBG("%s(%p,%p)",FUNC_NAME,req,call_profile);
	DBG("method: %s",req->method.c_str());

	if(call_profile){
		Cdr *cdr = new Cdr(*call_profile);
		cdr->update(*req);
		cdr->update(Start);
		cdr->refuse(*call_profile);
		router_mutex.lock();
		router->align_cdr(*cdr);
		router->write_cdr(cdr,true);
		router_mutex.unlock();
	}
}

bool Yeti::init(SBCCallLeg *call, const map<string, string> &values) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	CallCtx *ctx = getCtx(call);
	Cdr *cdr = getCdr(ctx);

	ctx->inc();

	if(call->isALeg()){
		SBCCallProfile &profile = call->getCallProfile();

		ostringstream ss;
		ss <<	config.msg_logger_dir << '/' <<
				call->getLocalTag() << "_" <<
				int2str(config.node_id) << ".pcap";
		profile.set_logger_path(ss.str());

		if(profile.global_tag.empty())
			profile.global_tag = call->getLocalTag();

		cdr->update_sbc(profile);
	} else {
		SBCCallProfile &profile = call->getCallProfile();
		if(!profile.callid.empty()){
			string id = AmSession::getNewId();
			replace(profile.callid,"%uuid",id);
		}
	}
	cdr->update(*call);
	return true;
}

void Yeti::onStateChange(SBCCallLeg *call, const CallLeg::StatusChangeCause &cause){
	string reason;
	getCtx_void();
	SBCCallLeg::CallStatus status = call->getCallStatus();
	Cdr *cdr = getCdr(ctx);
	bool aleg = call->isALeg();
	int internal_disconnect_code = 0;

	DBG("Yeti::onStateChange(%p) a_leg = %d",
		call,call->isALeg());

	if(!aleg){
		switch(status){
		case CallLeg::Ringing: {
			const SBCCallProfile &profile = call->getCallProfile();
			if(profile.ringing_timeout > 0)
			call->setTimer(YETI_RINGING_TIMEOUT_TIMER,profile.ringing_timeout);
		} break;
		case CallLeg::Connected:
			call->removeTimer(YETI_RINGING_TIMEOUT_TIMER);
			break;
		default:
			break;
		}
	}

	switch(cause.reason){
		case CallLeg::StatusChangeCause::SipReply:
			if(cause.param.reply!=NULL){
				if(aleg && status==CallLeg::Disconnected)
					cdr->update_bleg_reason(cause.param.reply->reason,
												cause.param.reply->code);
				reason = "SipReply. code = "+int2str(cause.param.reply->code);
			} else
				reason = "SipReply. empty reply";
			break;
		case CallLeg::StatusChangeCause::SipRequest:
			if(cause.param.request!=NULL){
				if(aleg && status==CallLeg::Disconnected)
					cdr->update_bleg_reason(cause.param.request->method,
												cause.param.request->method==SIP_METH_BYE?200:500);
				reason = "SipRequest. method = "+cause.param.request->method;
			} else
				reason = "SipRequest. empty request";
			break;
		case CallLeg::StatusChangeCause::Canceled:
			reason = "Canceled";
			break;
		case CallLeg::StatusChangeCause::NoAck:
			reason = "NoAck";
			internal_disconnect_code = DC_NO_ACK;
			break;
		case CallLeg::StatusChangeCause::NoPrack:
			reason = "NoPrack";
			internal_disconnect_code = DC_NO_PRACK;
			break;
		case CallLeg::StatusChangeCause::RtpTimeout:
			reason = "RtpTimeout";
			break;
		case CallLeg::StatusChangeCause::SessionTimeout:
			reason = "SessionTimeout";
			internal_disconnect_code = DC_SESSION_TIMEOUT;
			break;
		case CallLeg::StatusChangeCause::InternalError:
			reason = "InternalError";
			internal_disconnect_code = DC_INTERNAL_ERROR;
			break;
		case CallLeg::StatusChangeCause::Other:
			/*if(cause.param.desc!=NULL)
				reason = string("Other. ")+cause.param.desc;
			else
				reason = "Other. empty desc";
			internal_disconnect_code = DC_INTERNAL_ERROR;
			*/
			break;
		default:
			reason = "???";
	}

	if(aleg && internal_disconnect_code && status==CallLeg::Disconnected){
		unsigned int internal_code,response_code;
		string internal_reason,response_reason;

		CodesTranslator::instance()->translate_db_code(
			internal_disconnect_code,
			internal_code,internal_reason,
			response_code,response_reason,
			ctx->getOverrideId());
		cdr->update_internal_reason(DisconnectByTS,internal_reason,internal_code);
	}

	DBG("%s(%p,leg%s,state = %s, cause = %s)",FUNC_NAME,call,aleg?"A":"B",
		callStatus2str(status),
		reason.c_str());

}

void Yeti::onDestroyLeg(SBCCallLeg *call){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	getCtx_void();
	if(ctx->dec_and_test()){
		onLastLegDestroy(ctx,call);
		ctx->router->release(routers);
		call->setLogicData(NULL);
		delete ctx;
	} else {
		if(NULL!=ctx->getCurrentProfile())
			rctl.put(ctx->getCurrentResourceList());
		call->setLogicData(NULL);
	}
}

void Yeti::onLastLegDestroy(CallCtx *ctx,SBCCallLeg *call){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	if(!ctx->cdr_processed){
		Cdr *cdr = getCdr(ctx);
		// remove from active calls
		cdr_list.erase(cdr);
		//write cdr (Cdr class will be deleted by CdrWriter)
		ctx->router->write_cdr(cdr,true);
	}
}

CCChainProcessing Yeti::onBLegRefused(SBCCallLeg *call, AmSipReply& reply) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	getCtx_chained();
	Cdr* cdr = getCdr(ctx);
	CodesTranslator *ct = CodesTranslator::instance();
	unsigned int intermediate_code;
	string intermediate_reason;

	if(call->isALeg()){

		cdr->update(reply);
		cdr->update_bleg_reason(reply.reason,reply.code);

		ct->rewrite_response(reply.code,reply.reason,
							 intermediate_code,intermediate_reason,
							 ctx->getOverrideId(false)); //bleg_override_id
		ct->rewrite_response(intermediate_code,intermediate_reason,
							 reply.code,reply.reason,
							 ctx->getOverrideId(true)); //aleg_override_id
		cdr->update_internal_reason(DisconnectByDST,intermediate_reason,intermediate_code);
		cdr->update_aleg_reason(reply.reason,reply.code);

		if(ct->stop_hunting(reply.code,ctx->getOverrideId(false))){
			DBG("stop hunting");
		} else {
			DBG("continue hunting");

			//put current resources
			rctl.put(ctx->getCurrentResourceList());

			if(ctx->initial_invite!=NULL){
				if(chooseNextProfile(call)){
					DBG("%s() has new profile, so create new callee",FUNC_NAME);
					cdr = getCdr(ctx);

					if(0!=cdr_list.insert(cdr)){
						ERROR("onBLegRefused(): double insert into active calls list. integrity threat");
						ERROR("ctx: attempt = %d, cdr.logger_path = %s",
							ctx->attempt_num,cdr->msg_logger_path.c_str());
					} else {
						AmSipRequest &req = *ctx->initial_invite;
						connectCallee(call,req);
					}
				} else {
					DBG("%s() no new profile, just finish as usual",FUNC_NAME);
				}
			} else {
				ERROR("%s() intial_invite == NULL",FUNC_NAME);
			}
		} //stop_hunting
	} //call->isALeg()

	return ContinueProcessing;
}

CCChainProcessing Yeti::onInitialInvite(SBCCallLeg *call, InitialInviteHandlerParams &params) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");

	SqlCallProfile *profile = NULL;
	AmSipRequest &req = *params.aleg_modified_invite;
	AmSipRequest &b_req = *params.modified_invite;

	CallCtx *ctx = getCtx(call);
	Cdr *cdr = getCdr(ctx);
	ResourceCtlResponse rctl_ret;
	string refuse_reason;
	int refuse_code;
	int attempt = 0;

	PROF_START(func);

	cdr->update(Start);
	cdr->update(req);

	ctx->initial_invite = new AmSipRequest(req);

	//throw AmSession::Exception(NO_REPLY_DISCONNECT_CODE,"test silent mode");

	try {
	PROF_START(rchk);
	do {
		DBG("%s() check resources for profile. attempt %d",FUNC_NAME,attempt);
		rctl_ret = rctl.get(ctx->getCurrentResourceList(),refuse_code,refuse_reason);

		if(rctl_ret == RES_CTL_OK){
			DBG("%s() check resources succ",FUNC_NAME);
			break;
		} else if(	rctl_ret ==  RES_CTL_REJECT ||
					rctl_ret ==  RES_CTL_ERROR){
			DBG("%s() check resources failed with code: %d, reply: <%d '%s'>",FUNC_NAME,
				rctl_ret,refuse_code,refuse_reason.c_str());
			break;
		} else if(	rctl_ret == RES_CTL_NEXT){
			DBG("%s() check resources failed with code: %d, reply: <%d '%s'>",FUNC_NAME,
				rctl_ret,refuse_code,refuse_reason.c_str());

			profile = ctx->getNextProfile(true);

			if(NULL==profile){
				DBG("%s() there are no profiles more",FUNC_NAME);
				throw AmSession::Exception(503,"no more profiles");
			}

			DBG("%s() choosed next profile",FUNC_NAME);
			ParamReplacerCtx rctx(profile);
			if(check_and_refuse(profile,cdr,req,rctx)){
				throw AmSession::Exception(cdr->disconnect_rewrited_code,
										   cdr->disconnect_rewrited_reason);
			}
		}
		attempt++;
	} while(rctl_ret != RES_CTL_OK);

	if(rctl_ret != RES_CTL_OK){
		throw AmSession::Exception(refuse_code,refuse_reason);
	}

	PROF_END(rchk);
	PROF_PRINT("resources checking and grabbing",rchk);

	if(attempt != 0){
			//profile changed
			//we must update profile for leg
			DBG("%s() update call profile for leg",FUNC_NAME);
			call->getCallProfile() = *profile;
	}

	SBCCallProfile &call_profile = call->getCallProfile();
	//filterSDP
	int res = negotiateRequestSdp(call_profile,
							  req,
							  ctx->aleg_negotiated_media,
							  req.method,
							  call_profile.aleg_single_codec,
							  call_profile.static_codecs_aleg_id);
	if(res < 0){
		INFO("onInitialInvite() Not acceptable codecs");
		throw InternalException(FC_CODECS_NOT_MATCHED);
		//throw AmSession::Exception(488, SIP_REPLY_NOT_ACCEPTABLE_HERE);
	}

	//next we should filter request for legB
	res = filterRequestSdp(call,
						   call_profile,
						   b_req.body,b_req.method,
						   call_profile.static_codecs_bleg_id);
	if(res < 0){
		INFO("onInitialInvite() Not acceptable codecs for legB");
		throw AmSession::Exception(488, SIP_REPLY_NOT_ACCEPTABLE_HERE);
	}

	if(cdr->time_limit){
		DBG("%s() save timer %d with timeout %d",FUNC_NAME,
			YETI_CALL_DURATION_TIMER,
			cdr->time_limit);
		call->saveCallTimer(YETI_CALL_DURATION_TIMER,cdr->time_limit);
	}

	if(0!=cdr_list.insert(cdr)){
		ERROR("onInitialInvite(): double insert into active calls list. integrity threat");
		ERROR("ctx: attempt = %d, cdr.logger_path = %s",
			ctx->attempt_num,cdr->msg_logger_path.c_str());
		log_stacktrace(L_ERR);
		throw AmSession::Exception(500,SIP_REPLY_SERVER_INTERNAL_ERROR);
	}

	} catch(InternalException &e) {
		DBG("onInitialInvite() catched InternalException(%d)",e.icode);
		rctl.put(ctx->getCurrentResourceList());
		cdr->update_internal_reason(DisconnectByTS,e.internal_reason,e.internal_code);
		throw AmSession::Exception(e.response_code,e.response_reason);
	} catch(AmSession::Exception &e) {
		DBG("onInitialInvite() catched AmSession::Exception(%d,%s)",
			e.code,e.reason.c_str());
		rctl.put(ctx->getCurrentResourceList());
		//!TODO: rewrite response here
		cdr->update_internal_reason(DisconnectByTS,e.reason,e.code);
		throw e;
	}

	PROF_END(func);
	PROF_PRINT("Yeti::onInitialInvite",func);

	return ContinueProcessing;
}

void Yeti::onInviteException(SBCCallLeg *call,int code,string reason,bool no_reply){
	DBG("%s(%p,leg%s) %d:'%s' no_reply = %d",FUNC_NAME,call,call->isALeg()?"A":"B",
		code,reason.c_str(),no_reply);
	getCtx_void();
	Cdr *cdr = getCdr(ctx);
	cdr->lock();
	cdr->disconnect_initiator = DisconnectByTS;
	if(cdr->disconnect_internal_code==0){ //update only if not previously was setted
		cdr->disconnect_internal_code = code;
		cdr->disconnect_internal_reason = reason;
	}
	if(!no_reply){
		cdr->disconnect_rewrited_code = code;
		cdr->disconnect_rewrited_reason = reason;
	}
	cdr->unlock();
}

CCChainProcessing Yeti::onInDialogRequest(SBCCallLeg *call, const AmSipRequest &req) {
	DBG("%s(%p,leg%s) '%s'",FUNC_NAME,call,call->isALeg()?"A":"B",req.method.c_str());
	if(call->isALeg()){
		if(req.method==SIP_METH_CANCEL){
			getCdr(call)->update_internal_reason(DisconnectByORG,"Request terminated (Cancel)",487);
		}
	}
	return ContinueProcessing;
}

CCChainProcessing Yeti::onInDialogReply(SBCCallLeg *call, const AmSipReply &reply) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	if(!call->isALeg()){
		getCdr(call)->update(reply);
	}
	return ContinueProcessing;
}

CCChainProcessing Yeti::onEvent(SBCCallLeg *call, AmEvent *e) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");

	AmRtpTimeoutEvent *rtp_event = dynamic_cast<AmRtpTimeoutEvent*>(e);
	if(rtp_event){
		DBG("rtp event id: %d",rtp_event->event_id);
		return onRtpTimeout(call,*rtp_event);
	}

	AmSipRequestEvent *request_event = dynamic_cast<AmSipRequestEvent*>(e);
	if(request_event){
		DBG("request event method: %s",
			request_event->req.method.c_str());
	}

	AmSipReplyEvent *reply_event = dynamic_cast<AmSipReplyEvent*>(e);
	if(reply_event){
		DBG("reply event  code: %d, reason:'%s'",
			reply_event->reply.code,reply_event->reply.reason.c_str());
		//!TODO: find appropiate way to avoid hangup in disconnected state
		if(reply_event->reply.code==408 && call->getCallStatus()==CallLeg::Disconnected){
			ERROR("received 408 in disconnected state");
			throw AmSession::Exception(500,SIP_REPLY_SERVER_INTERNAL_ERROR);
		}
	}

	AmPluginEvent* plugin_event = dynamic_cast<AmPluginEvent*>(e);
	if(plugin_event){
		DBG("%s plugin_event. name = %s, event_id = %d",FUNC_NAME,
			plugin_event->name.c_str(),
			plugin_event->event_id);
		if(plugin_event->name=="timer_timeout"){
			onTimerEvent(call,plugin_event->data.get(0).asInt());
		}
	}

	SBCControlEvent* sbc_event = dynamic_cast<SBCControlEvent*>(e);
	if(sbc_event){
		DBG("sbc event id: %d, cmd: %s",sbc_event->event_id,sbc_event->cmd.c_str());
		onControlEvent(call,sbc_event);
	}

	if (e->event_id == E_SYSTEM) {
		AmSystemEvent* sys_ev = dynamic_cast<AmSystemEvent*>(e);
		if(sys_ev){
			DBG("sys event type: %d",sys_ev->sys_event);
			onSystemEvent(call,sys_ev);
		}
	}
	return ContinueProcessing;
}


CCChainProcessing Yeti::onRtpTimeout(SBCCallLeg *call,const AmRtpTimeoutEvent &rtp_event){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	unsigned int internal_code,response_code;
	string internal_reason,response_reason;

	getCtx_chained();

	if(call->getCallStatus()!=CallLeg::Connected){
		WARN("module catched RtpTimeout in no Connected state. ignore it");
		return StopProcessing;
	}

	CodesTranslator::instance()->translate_db_code(
		DC_RTP_TIMEOUT,
		internal_code,internal_reason,
		response_code,response_reason,
		ctx->getOverrideId());
	Cdr *cdr = getCdr(ctx);
	cdr->update_internal_reason(DisconnectByTS,internal_reason,internal_code);
	cdr->update_aleg_reason("Bye",200);
	cdr->update_bleg_reason("Bye",200);
	return ContinueProcessing;
}

CCChainProcessing Yeti::onSystemEvent(SBCCallLeg *call,AmSystemEvent* event){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	if (event->sys_event == AmSystemEvent::ServerShutdown) {
		onServerShutdown(call);
	}
	return ContinueProcessing;
}

CCChainProcessing Yeti::onTimerEvent(SBCCallLeg *call,int timer_id){
	DBG("%s(%p,%d,leg%s)",FUNC_NAME,call,timer_id,call->isALeg()?"A":"B");

	Cdr *cdr = getCdr(call);

	switch(timer_id){
	case YETI_CALL_DURATION_TIMER:
		cdr->update_internal_reason(DisconnectByTS,"Call duration limit reached",200);
		cdr->update_aleg_reason("Bye",200);
		cdr->update_bleg_reason("Bye",200);
		break;
	case YETI_RINGING_TIMEOUT_TIMER:
		cdr->update_internal_reason(DisconnectByTS,"Ringing timeout",408);
		AmSessionContainer::instance()->postEvent(
					call->getLocalTag(),
					new B2BEvent(B2BTerminateLeg));
		break;
	default:
		cdr->update_internal_reason(DisconnectByTS,"Timer "+int2str(timer_id)+" fired",200);
		break;
	}
	return ContinueProcessing;
}

CCChainProcessing Yeti::onControlEvent(SBCCallLeg *call,SBCControlEvent *event){
	DBG("%s(%p,leg%s) cmd = %s, event_id = %d",FUNC_NAME,call,call->isALeg()?"A":"B",
			event->cmd.c_str(),event->event_id);
	if(event->cmd=="teardown"){
		return onTearDown(call);
	}
	return ContinueProcessing;
}

void Yeti::onServerShutdown(SBCCallLeg *call){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	getCtx_void();
	getCdr(ctx)->update_internal_reason(DisconnectByTS,"ServerShutdown",200);
	//may never reach onDestroy callback so free resources here
	rctl.put(ctx->getCurrentResourceList());
}

CCChainProcessing Yeti::onTearDown(SBCCallLeg *call){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	getCtx_chained();
	Cdr *cdr = getCdr(ctx);
	cdr->update_internal_reason(DisconnectByTS,"Teardown",200);
	cdr->update_aleg_reason("Bye",200);
	cdr->update_bleg_reason("Bye",200);
	return ContinueProcessing;
}

CCChainProcessing Yeti::putOnHold(SBCCallLeg *call) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	return ContinueProcessing;
}

CCChainProcessing Yeti::resumeHeld(SBCCallLeg *call, bool send_reinvite) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	return ContinueProcessing;
}

CCChainProcessing Yeti::createHoldRequest(SBCCallLeg *call, AmSdp &sdp) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	return ContinueProcessing;
}

CCChainProcessing Yeti::handleHoldReply(SBCCallLeg *call, bool succeeded) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	return ContinueProcessing;
}

CCChainProcessing Yeti::onRemoteDisappeared(SBCCallLeg *call, const AmSipReply &reply){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	getCtx_chained();
	if(call->isALeg()){
		//trace available values
		if(ctx->initial_invite!=NULL){
			AmSipRequest &req = *ctx->initial_invite;
			DBG("req.method = '%s'",req.method.c_str());
		} else {
			ERROR("intial_invite == NULL");
		}
		getCdr(ctx)->update_internal_reason(DisconnectByTS,reply.reason,reply.code);
	}
	return ContinueProcessing;
}

CCChainProcessing Yeti::onBye(SBCCallLeg *call, const AmSipRequest &req){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	getCtx_chained();
	Cdr *cdr = getCdr(ctx);
	cdr->update_bleg_reason("onBye",200); //set bleg_reason anyway to onBye
	if(call->isALeg()){
		cdr->update_internal_reason(DisconnectByORG,"onBye",200);
	} else {
		cdr->update_internal_reason(DisconnectByDST,"onOtherBye",200);
	}
	return ContinueProcessing;
}

CCChainProcessing Yeti::onOtherBye(SBCCallLeg *call, const AmSipRequest &req){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	getCtx_chained();
	if(call->isALeg()){
		if(call->getCallStatus()==CallLeg::NoReply){
			//avoid considering of bye in early state as succ call
			ERROR("received OtherBye in NoReply state");
			Cdr *cdr = getCdr(ctx);
			cdr->update_internal_reason(DisconnectByDST,"onEarlyOtherBye",500);
			cdr->update_aleg_reason("Request terminated",487);
			cdr_list.erase(cdr);
			ctx->cdr_processed = true;
			router->write_cdr(cdr,true);
		}
	}
	return ContinueProcessing;
}

void Yeti::onCallConnected(SBCCallLeg *call, const AmSipReply& reply){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	getCtx_void();
	if(call->isALeg()){
		Cdr *cdr = getCdr(ctx);
		cdr->update(Connect);
	}
}

void Yeti::onRTPStreamDestroy(SBCCallLeg *call,AmRtpStream *stream){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	getCtx_void();
	getCdr(ctx)->update(call,stream);
}

void Yeti::onSdpCompleted(SBCCallLeg *call, AmSdp& offer, AmSdp& answer){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	if(call->isALeg()){
		//fix sdp for relay mask computing in B -> A direction
		answer.media = getCtx(call)->aleg_negotiated_media;
	}
}

int Yeti::relayEvent(SBCCallLeg *call, AmEvent *e){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	CallCtx *ctx = getCtx(call);
	if(NULL==ctx) {
		ERROR("Yeti::relayEvent(%p) zero ctx. ignore event",call);
		delete e;
		return 1;
	}

	bool a_leg = call->isALeg();

	switch (e->event_id) {
		case B2BSipRequest: {
			B2BSipRequestEvent* req_ev = dynamic_cast<B2BSipRequestEvent*>(e);
			assert(req_ev);

			DBG("Yeti::relayEvent(%p) filtering request '%s' (c/t '%s')\n",
				call,req_ev->req.method.c_str(), req_ev->req.body.getCTStr().c_str());

			SBCCallProfile &call_profile = call->getCallProfile();

			int static_codecs_negotiate_id;
			int static_codecs_filter_id;
			bool single_codec;
			vector<SdpMedia> * negotiated_media;

			if(a_leg){
				static_codecs_negotiate_id = call_profile.static_codecs_aleg_id;
				static_codecs_filter_id = call_profile.static_codecs_bleg_id;
				single_codec = call_profile.aleg_single_codec;
				negotiated_media = &ctx->aleg_negotiated_media;
			} else {
				static_codecs_negotiate_id = call_profile.static_codecs_bleg_id;
				static_codecs_filter_id = call_profile.static_codecs_aleg_id;
				single_codec = call_profile.bleg_single_codec;
				negotiated_media = &ctx->bleg_negotiated_media;
			}

			int res = negotiateRequestSdp(call_profile,
										  req_ev->req,
										  *negotiated_media,
										  req_ev->req.method,
										  single_codec,
										  static_codecs_negotiate_id);
			if (res < 0) {
				delete e;
				return res;
			}
			res = filterRequestSdp(call,
								   call_profile,
								   req_ev->req.body, req_ev->req.method,
								   static_codecs_filter_id);
			if (res < 0) {
				delete e;
				return res;
			}
		} break;

		case B2BSipReply: {
			B2BSipReplyEvent* reply_ev = dynamic_cast<B2BSipReplyEvent*>(e);
			assert(reply_ev);

			DBG("Yeti::relayEvent(%p) filtering body for reply '%s' (c/t '%s')\n",
				call,reply_ev->trans_method.c_str(), reply_ev->reply.body.getCTStr().c_str());
			filterReplySdp(call,
						   reply_ev->reply.body, reply_ev->reply.cseq_method,
						   call->isALeg() ? ctx->bleg_negotiated_media : ctx->aleg_negotiated_media);
		} break;
	} //switch(e->event_id)
	return 0;
}
	//!Ood handlers

bool Yeti::init(SBCCallProfile &profile, SimpleRelayDialog *relay, void *&user_data) {
	DBG("%s() called",FUNC_NAME);
	return true;
}

void Yeti::initUAC(const AmSipRequest &req, void *user_data) {
	DBG("%s() called",FUNC_NAME);
}

void Yeti::initUAS(const AmSipRequest &req, void *user_data) {
	DBG("%s() called",FUNC_NAME);
}

void Yeti::finalize(void *user_data) {
	DBG("%s() called",FUNC_NAME);
}

void Yeti::onSipRequest(const AmSipRequest& req, void *user_data) {
	DBG("%s() called",FUNC_NAME);
}

void Yeti::onSipReply(	const AmSipRequest& req,
						const AmSipReply& reply,
						AmBasicSipDialog::Status old_dlg_status,
						void *user_data)
{
	DBG("%s() called",FUNC_NAME);
}

void Yeti::onB2BRequest(const AmSipRequest& req, void *user_data) {
	DBG("%s() called",FUNC_NAME);
}

void Yeti::onB2BReply(const AmSipReply& reply, void *user_data) {
	DBG("%s() called",FUNC_NAME);
}

	//!xmlrpc handlers

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
		if(cdr_list.getCall(local_tag,calls,router)){
			ret.push(200);
			ret.push(calls);
		} else {
			ret.push(404);
			ret.push("Have no CDR with such local tag");
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
		p["compiled_at"] = YETI_BUILD_DATE;
		p["compiled_by"] = YETI_BUILD_USER;
		ret.push(p);
}

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

	bool compute_cost = args.size() && args[0] == "cost";
	string path = args.size()>1 ? args[1].asCStr() : DEFAULT_BECH_FILE_PATH;

	const AmPlugIn* plugin = AmPlugIn::instance();
	plugin->getPayloads(payloads);

	if(compute_cost){
		size = load_testing_source(path,buf);
		compute_cost = size > 0;
	}
	DBG("compute_cost = %d",compute_cost);

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
