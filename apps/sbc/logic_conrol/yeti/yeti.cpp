#include "yeti.h"

#include "log.h"
#include "AmPlugIn.h"
#include "AmArg.h"
#include "AmSession.h"
#include "CallLeg.h"
#include "Version.h"
#include "RegisterDialog.h"
#include "SBC.h"
struct CallLegCreator;

#include <string.h>

class YetiFactory : public AmDynInvokeFactory
{
public:
    YetiFactory(const string& name)
	: AmDynInvokeFactory(name) {}

    ~YetiFactory(){
        DBG("~YetiFactory()");
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

Yeti* Yeti::instance()
{
    if(!_instance)
    _instance = new Yeti();
    return _instance;
}

Yeti::Yeti():
    router(new SqlRouter())
//    cdr_writer(new CdrWriter())
{
	routers.insert(router);
    DBG("Yeti()");
}


Yeti::~Yeti() {
    DBG("~Yeti()");
	router->release(routers);
	rctl.stop();
}

bool Yeti::read_config(){
	if(cfg.loadFile(AmConfig::ModConfigPath + string(MOD_NAME ".conf"))) {
	  ERROR("No configuration for "MOD_NAME" present (%s)\n",
	   (AmConfig::ModConfigPath + string(MOD_NAME ".conf")).c_str()
	  );
	  return false;
	}
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
	//profile->cc_interfaces.push_back(self_iface);   //add reference to ourself

    DBG("p = %p",profile);
	if(rctl.configure(cfg)){
		ERROR("ResourceControl configure failed");
		return -1;
	}
	rctl.start();

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
        start(args[CC_API_PARAMS_CC_NAMESPACE].asCStr(),
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
        connect(args[CC_API_PARAMS_CC_NAMESPACE].asCStr(),
            args[CC_API_PARAMS_LTAG].asCStr(),
            call_profile,
            args[CC_API_PARAMS_OTHERID].asCStr(),
            args[CC_API_PARAMS_TIMESTAMPS][CC_API_TS_CONNECT_SEC].asInt(),
            args[CC_API_PARAMS_TIMESTAMPS][CC_API_TS_CONNECT_USEC].asInt()
        );
  } else if(method == "end"){
        SBCCallProfile* call_profile =
            dynamic_cast<SBCCallProfile*>(args[CC_API_PARAMS_CALL_PROFILE].asObject());
        end(args[CC_API_PARAMS_CC_NAMESPACE].asCStr(),
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
  } else if (method == "getConfig"){
    INFO ("getConfig received via xmlrpc2di");
    GetConfig(args,ret);
  } else if (method == "showVersion"){
    INFO ("showVersion received via xmlrpc2di");
    showVersion(args, ret);
  } else if(method == "reload"){
	INFO ("reload received via xmlrpc2di");
	reload(args,ret);
  } else if(method == "_list"){
    ret.push(AmArg("showVersion"));
    ret.push(AmArg("getConfig"));
    ret.push(AmArg("getStats"));
    ret.push(AmArg("clearStats"));
    ret.push(AmArg("dropCall"));
    ret.push(AmArg("getCall"));
    ret.push(AmArg("getCalls"));
    ret.push(AmArg("getCallsCount"));
	ret.push(AmArg("reload"));
  }
  else
    throw AmDynInvoke::NotImplemented(method);
}


SBCCallProfile& Yeti::getCallProfile( const AmSipRequest& req,
                                        ParamReplacerCtx& ctx )
{
    DBG("%s() called",FUNC_NAME);
    return *profile;
}

SBCCallLeg *Yeti::getCallLeg( const AmSipRequest& req,
                        ParamReplacerCtx& ctx,
                        CallLegCreator *leg_creator ){
    DBG("%s() called",FUNC_NAME);
	router_mutex.lock();
	SqlRouter *r = router;
	router_mutex.unlock();
	CallCtx *call_ctx = new CallCtx(r);
	r->getprofiles(req,*call_ctx);

	SqlCallProfile *profile = call_ctx->getFirstProfile();
	if(NULL == profile){
		r->release(routers);
		delete call_ctx;
		throw AmSession::Exception(500, SIP_REPLY_SERVER_INTERNAL_ERROR);
	}

	Cdr *cdr = getCdr(call_ctx);

	if(check_and_refuse(profile,cdr,req,ctx,true)) {
		r->write_cdr(cdr,true);
		r->release(routers);
		delete call_ctx;
		return NULL;
	}

	SBCCallProfile& call_profile = *profile;

	/*if(!call_profile.refuse_with.empty()) {
      if(call_profile.refuse(ctx, req) < 0) {
        throw AmSession::Exception(500, SIP_REPLY_SERVER_INTERNAL_ERROR);
      }
      cdr->update(Start);
      cdr->update(req);
	  cdr->update_sbc(*profile);
	  cdr->refuse(*profile);
	  router->write_cdr(cdr,true);
	  delete call_ctx;
      return NULL;
	}*/

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

	call_profile.sst_aleg_enabled =
	  ctx.replaceParameters(call_profile.sst_aleg_enabled,
				"enable_aleg_session_timer", orig_req);

	call_profile.sst_enabled = ctx.replaceParameters(call_profile.sst_enabled,
							 "enable_session_timer", orig_req);

	if ((call_profile.sst_aleg_enabled == "yes") ||
		(call_profile.sst_enabled == "yes")) {

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
	}
	else if(call_profile.reg_caching) {
	  // REG-Cache lookup
	  uac_req.r_uri = call_profile.retarget(orig_req.user,*call->dlg);
	}

	string ruri, to, from;

	ruri = call_profile.ruri.empty() ? uac_req.r_uri : call_profile.ruri;
	if(!call_profile.ruri_host.empty()){
	  ctx.ruri_parser.uri = ruri;
	  if(!ctx.ruri_parser.parse_uri()) {
		WARN("Error parsing R-URI '%s'\n", ruri.c_str());
	  }
	  else {
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

	inplaceHeaderFilter(invite_req.hdrs, call_profile.headerfilter);

	if (call_profile.append_headers.size() > 2) {
	  string append_headers = call_profile.append_headers;
	  assertEndCRLF(append_headers);
	  invite_req.hdrs+=append_headers;
	}

	int res = call->filterSdp(invite_req.body, invite_req.method);
	if (res < 0) {
	  // FIXME: quick hack, throw the exception from the filtering function for
	  // requests
	  throw AmSession::Exception(488, SIP_REPLY_NOT_ACCEPTABLE_HERE);
	}

	call->connectCallee(to, ruri, from, orig_req, invite_req);

	return false;
}

bool Yeti::chooseNextProfile(SBCCallLeg *call){
	DBG("%s()",FUNC_NAME);

	string refuse_reason;
	int refuse_code;
	CallCtx *ctx = getCtx(call);
	Cdr *cdr = getCdr(ctx);
	SqlCallProfile *profile = NULL;
	ResourceCtlResponse rctl_ret;
	bool has_profile = false;

	list<SqlCallProfile *>::iterator p = ctx->current_profile;
	profile = ctx->getNextProfile(false);

	//if(NULL==profile || !profile->refuse_with.empty()){
	if(NULL==profile || profile->disconnect_code_id!=0){
		//pretend that nothing happen. we were never called
		//remove excess cdr and replace ctx pointer with old
		DBG("%s() no more profiles or refuse profile on serial fork. ignore it",FUNC_NAME);
		delete ctx->cdr;
		ctx->cdr = cdr;
		ctx->current_profile = p;
		return false;
	}

	//write all cdr and replacy ctx pointer with new
	cdr_list.erase_lookup_key(&cdr->local_tag);
	ctx->router->write_cdr(cdr,false);
	cdr = getCdr(ctx);

	do {
		if(NULL==profile){
			DBG("%s() there are no profiles more",FUNC_NAME);
			//cdr->update(DisconnectByTS,"no more profiles",503);
			//throw AmSession::Exception(503,"no more profiles");
			break;
		}

		DBG("%s() choosed next profile. check it for refuse",FUNC_NAME);

		ParamReplacerCtx rctx(profile);
		if(check_and_refuse(profile,cdr,*ctx->initial_invite,rctx)){
			DBG("%s() profile contains refuse code",FUNC_NAME);
			break;
		}
		/*
		if(!profile->refuse_with.empty()) {
			DBG("%s() profile contains nonempty refuse_with ",FUNC_NAME);
			cdr->refuse(*profile);
			break;
			//throw AmSession::Exception(cdr->disconnect_code,cdr->disconnect_reason);
		}*/

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
			//write old cdr here
			profile = ctx->getNextProfile(false);
			ctx->router->write_cdr(cdr,true);
			cdr = getCdr(ctx);
		}
	} while(rctl_ret != RES_CTL_OK);

	if(!has_profile){
		cdr->update(DisconnectByTS,refuse_reason,refuse_code);
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
	if(profile->disconnect_code_id==0)
		return false;
	//translation here
	unsigned int internal_code,response_code;
	string internal_reason,response_reason;
	CodesTranslator::instance()->translate_db_code(profile->disconnect_code_id,
							 internal_code,internal_reason,
							 response_code,response_reason);
	//fill && write cdr
	cdr->update(DisconnectByDB,internal_reason,internal_code);
	cdr->update_rewrited(response_reason,response_code);

	if(send_reply){
		cdr->update(Start);
		cdr->update(req);
		cdr->update_sbc(*profile);
		//prepare & send sip response
		string hdrs = ctx.replaceParameters(profile->append_headers, "append_headers", req);
		if (hdrs.size()>2) assertEndCRLF(hdrs);
		AmSipDialog::reply_error(req, response_code, response_reason, hdrs);
	}
	return true;
}
    //!InDialog handlers
void Yeti::start(const string& cc_name, const string& ltag,
               SBCCallProfile* call_profile,
               int start_ts_sec, int start_ts_usec,
               const AmArg& values, int timer_id, AmArg& res) {
    DBG("%s(%p,%s)",FUNC_NAME,call_profile,ltag.c_str());
    res.push(AmArg());
}

void Yeti::connect(const string& cc_name, const string& ltag,
             SBCCallProfile* call_profile,
             const string& other_tag,
             int connect_ts_sec, int connect_ts_usec) {
    DBG("%s(%p,%s)",FUNC_NAME,call_profile,ltag.c_str());
}

void Yeti::end(const string& cc_name, const string& ltag,
             SBCCallProfile* call_profile,
             int end_ts_sec, int end_ts_usec) {
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

void Yeti::init(SBCCallLeg *call, const map<string, string> &values) {
	DBG("%s(%p,leg%s) LogicData = %p",FUNC_NAME,call,call->isALeg()?"A":"B",call->getLogicData());
	CallCtx *ctx = getCtx(call);
	SBCCallProfile &profile = call->getCallProfile();
	Cdr *cdr = getCdr(ctx);
	ctx->inc();
	if(call->isALeg()){
		replace(profile.msg_logger_path,"%ltag",call->getLocalTag());
		cdr->update_sbc(profile);
	}
	cdr->update(*call);
}

void Yeti::onStateChange(SBCCallLeg *call){
	DBG("%s(%p,leg%s,state = %d)",FUNC_NAME,call,call->isALeg()?"A":"B",call->getCallStatus());
    /*
    from CallLeg.h
    enum CallStatus {
      Disconnected, //< there is no other call leg we are connected to
      NoReply,      //< there is at least one call leg we are connected to but without any response
      Ringing,      //< this leg or one of legs we are connected to rings
      Connected,    //< there is exactly one call leg we are connected to, in this case AmB2BSession::other_id holds the other leg id
      Disconnecting //< we were connected and now going to be disconnected (waiting for reINVITE reply for example)
    };
    */
}

void Yeti::onDestroyLeg(SBCCallLeg *call){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	CallCtx *ctx = getCtx(call);
	if(ctx->dec_and_test()){
		DBG("%s(%p,leg%s) ctx->dec_and_test() = true",FUNC_NAME,call,call->isALeg()?"A":"B");
		onLastLegDestroy(ctx,call);
		ctx->router->release(routers);
		delete ctx;
	} else {
		DBG("%s(%p,leg%s) ctx->dec_and_test() = false",FUNC_NAME,call,call->isALeg()?"A":"B");
	}
}

void Yeti::onLastLegDestroy(CallCtx *ctx,SBCCallLeg *call){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	Cdr *cdr = getCdr(ctx);
	// remove from active calls
	cdr_list.erase_lookup_key(&cdr->local_tag);
	//release resources
	if(NULL!=ctx->getCurrentProfile())
		rctl.put(ctx->getCurrentResourceList());
	//write cdr (Cdr class will be deleted by CdrWriter)
	ctx->router->write_cdr(cdr,true);
}

CCChainProcessing Yeti::onBLegRefused(SBCCallLeg *call, AmSipReply& reply) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	CallCtx *ctx = getCtx(call);
	Cdr* cdr = getCdr(ctx);


	if(call->isALeg()){

		cdr->update(reply);
		cdr->update(DisconnectByDST,reply.reason,reply.code);

		if(CodesTranslator::instance()->stop_hunting(reply.code)){
			DBG("stop hunting");
		} else {
			DBG("continue hunting");

			//put current resources
			rctl.put(ctx->getCurrentResourceList());

			if(ctx->initial_invite!=NULL){
				AmSipRequest &req = *ctx->initial_invite;
				//!TODO: get next callprofile here instead of current
				if(chooseNextProfile(call)){
					DBG("%s() has new profile, so create new callee",FUNC_NAME);
					cdr = getCdr(ctx);
					cdr_list.insert(&cdr->local_tag,cdr);
					connectCallee(call,req);
				} else {
					DBG("%s() no new profile, just finish as usual",FUNC_NAME);
				}
			} else {
				ERROR("%s() intial_invite == NULL",FUNC_NAME);
			}
		}
		/*
		string r;
		unsigned int c;
		*/
		CodesTranslator::instance()->rewrite_response(reply.code,reply.reason,
													  reply.code,reply.reason);
		cdr->update_rewrited(reply.reason,reply.code);
		//reply.code = c;
		//reply.reason = r;
	}

	return ContinueProcessing;
}

CCChainProcessing Yeti::onInitialInvite(SBCCallLeg *call, InitialInviteHandlerParams &params) {
    DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");

	SqlCallProfile *profile = NULL;
	const AmSipRequest &req = *params.original_invite;

	CallCtx *ctx = getCtx(call);
	Cdr *cdr = getCdr(ctx);
	ResourceCtlResponse rctl_ret;
	string refuse_reason;
	int refuse_code;
	int attempt = 0;

	cdr->update(Start);
	cdr->update(req);

	ctx->initial_invite = new AmSipRequest(req);
	try {
	do {
		DBG("%s() check resources for profile. attempt %d",FUNC_NAME,attempt);
		rctl_ret = rctl.get(ctx->getCurrentResourceList(),refuse_code,refuse_reason);

		if(rctl_ret == RES_CTL_OK){
			DBG("%s() check resources succ",FUNC_NAME);

			//profile = ctx->getCurrentProfile();
			//cdr->update_sbc(*profile);
			//ParamReplacerCtx rctx(profile);
			//cdr->replace(rctx,req);

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
			/*
			if(!profile->refuse_with.empty()) {
				DBG("%s() profile contains nonempty refuse_with ",FUNC_NAME);
				ParamReplacerCtx rctx(profile);
				cdr->update_sbc(*profile);
				cdr->replace(rctx,req);
				//if(profile->refuse(rctx, req) < 0) {
				//	throw AmSession::Exception(500, SIP_REPLY_SERVER_INTERNAL_ERROR);
				//}
				cdr->refuse(*profile);
				throw AmSession::Exception(cdr->disconnect_code,cdr->disconnect_reason);
			}*/
		}
		attempt++;
	} while(rctl_ret != RES_CTL_OK);

	if(rctl_ret != RES_CTL_OK){
		throw AmSession::Exception(refuse_code,refuse_reason);
	} else {
		if(attempt != 0){
			//profile changed
			//we must update profile for leg
			DBG("%s() update call profile for leg",FUNC_NAME);
			call->getCallProfile() = *profile;
		}
	}
	if(cdr->time_limit){
        DBG("%s() save timer %d with timeout %d",FUNC_NAME,
              SBC_TIMER_ID_CALL_TIMERS_START,
              cdr->time_limit);
        call->saveCallTimer(SBC_TIMER_ID_CALL_TIMERS_START,cdr->time_limit);
    }

	cdr_list.insert(&cdr->local_tag,cdr);
	} catch(AmSession::Exception &e) {
		DBG("onInitialInvite() catched AmSession::Exception(%d,%s)",
			e.code,e.reason.c_str());
		//!TODO: rewrite response here
		cdr->update(DisconnectByTS,e.reason,e.code);
		throw e;
	}

    return ContinueProcessing;
}

CCChainProcessing Yeti::onInDialogRequest(SBCCallLeg *call, const AmSipRequest &req) {
    DBG("%s(%p,leg%s) '%s'",FUNC_NAME,call,call->isALeg()?"A":"B",req.method.c_str());
    if(call->isALeg()){
        if(req.method==SIP_METH_CANCEL){
			getCdr(call)->update(DisconnectByORG,"Request terminated (Cancel)",487);
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

    if(call->isALeg()){
        AmPluginEvent* plugin_event = dynamic_cast<AmPluginEvent*>(e);
        if(plugin_event){
            DBG("%s plugin_event. name = %s, event_id = %d",FUNC_NAME,
                plugin_event->name.c_str(),
                plugin_event->event_id);
            if(plugin_event->name=="timer_timeout"){
                int timer_id = plugin_event->data.get(0).asInt();
                DBG("%s() timer %d timeout, stopping call\n",FUNC_NAME,timer_id);
				getCdr(call)->update(DisconnectByTS,"Balance timer",200);
            }
        }

        SBCControlEvent* sbc_event = dynamic_cast<SBCControlEvent*>(e);
        if(sbc_event){
            DBG("%s sbc control event. cmd = %s, event_id = %d",FUNC_NAME,
                sbc_event->cmd.c_str(),
                sbc_event->event_id);
            if(sbc_event->cmd == "teardown"){
                DBG("%s() teardown\n",FUNC_NAME);
				getCdr(call)->update(DisconnectByTS,"Teardown",200);
            }
        }

		if (e->event_id == E_SYSTEM) {
			AmSystemEvent* sys_ev = dynamic_cast<AmSystemEvent*>(e);
			if(sys_ev){
				DBG("Session received system Event");
				if (sys_ev->sys_event == AmSystemEvent::ServerShutdown) {
					DBG("ServerShutdown event");
					getCdr(call)->update(DisconnectByTS,"ServerShutdown",200);
				}
			}
		}
	}
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
	CallCtx *ctx = getCtx(call);
	if(call->isALeg()){
		//trace available values
		if(ctx->initial_invite!=NULL){
			AmSipRequest &req = *ctx->initial_invite;
			DBG("req.method = '%s'",req.method.c_str());
		} else {
			ERROR("intial_invite == NULL");
		}
		getCdr(ctx)->update(DisconnectByTS,reply.reason,reply.code);
	}
	return ContinueProcessing;
}

CCChainProcessing Yeti::onBye(SBCCallLeg *call, const AmSipRequest &req){
    DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    if(call->isALeg()){
		getCdr(call)->update(DisconnectByORG,"onBye",200);
    }
    return ContinueProcessing;
}

CCChainProcessing Yeti::onOtherBye(SBCCallLeg *call, const AmSipRequest &req){
    DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    if(call->isALeg()){
		getCdr(call)->update(DisconnectByDST,"onOtherBye",200);
    }
    return ContinueProcessing;
}

void Yeti::onCallConnected(SBCCallLeg *call, const AmSipReply& reply){
    DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    if(call->isALeg()){
		Cdr *cdr = getCdr(call);
        cdr->update(Connect);
    }
}
    //!Ood handlers
void Yeti::init(SBCCallProfile &profile, SimpleRelayDialog *relay, void *&user_data) {
    DBG("%s() called",FUNC_NAME);
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

void Yeti::onSipReply(const AmSipRequest& req,
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

	cdr_list.getCalls(calls,calls_show_limit,router);

    ret.push(200);
    ret.push(calls);
}

void Yeti::ClearStats(const AmArg& args, AmArg& ret){
    if(router)
      router->clearStats();
    ret.push(200);
    ret.push("OK");
}

void Yeti::GetStats(const AmArg& args, AmArg& ret){
  AmArg stats,underlying_stats;

  ret.push(200);
      /* Yeti stats */
  stats["calls_show_limit"] = (int)calls_show_limit;
      /* sql_router stats */
  router_mutex.lock();
  AmArg routers_stats;
  stats["active_routers_count"] = (long int)routers.size();
  set<SqlRouter *>::const_iterator i = routers.begin();
  for(;i!=routers.end();++i){
		underlying_stats.clear();
		SqlRouter *r = *i;
		if(r){
			router->getStats(underlying_stats);
			if(r == router){
				routers_stats.push("active",underlying_stats);
			} else {
				routers_stats.push("old",underlying_stats);
			}
		}
  }
  router_mutex.unlock();
  stats.push("routers",routers_stats);

  underlying_stats.clear();
  AmSessionContainer::instance()->getStats(underlying_stats);
  stats.push("AmSessionContainer",underlying_stats);

  underlying_stats.clear();
  underlying_stats["SessionNum"] = (int)AmSession::getSessionNum();
  underlying_stats["MaxSessionNum"] = (int)AmSession::getMaxSessionNum();
  underlying_stats["AvgSessionNum"] = (int)AmSession::getAvgSessionNum();
  underlying_stats["MaxCPS"] = (int)AmSession::getMaxCPS();
  underlying_stats["AvgCPS"] = (int)AmSession::getAvgCPS();

  stats.push("AmSession",underlying_stats);

  ret.push(stats);
}

void Yeti::GetConfig(const AmArg& args, AmArg& ret) {
  AmArg a;
  ret.push(200);
  if(router){
    router->getConfig(a);
    ret.push(a);
  }
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

void Yeti::reload(const AmArg& args, AmArg& ret){
	if(0==args.size()){
		ret.push(400);
		ret.push(AmArg());
		ret[1].push("resources");
		ret[1].push("translations");
		ret[1].push("router");
		return;
	}
	args.assertArrayFmt("s");

	if(!read_config()){
	  ret.push(500);
	  ret.push("config file reload failed");
	  return;
	}

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
