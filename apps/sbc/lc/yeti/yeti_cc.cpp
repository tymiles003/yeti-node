/*
 * contains CC, extCC handlers implementation
*/

#include "yeti.h"
#include "cdr/Cdr.h"
#include "SDPFilter.h"
#include "sdp_filter.h"
#include "dtmf_sip_info.h"

#include "RegisterDialog.h"
#include "AmSipMsg.h"

#define getCtx_void \
	CallCtx *ctx = getCtx(call);\
	if(NULL==ctx){\
		ERROR("CallCtx = nullptr ");\
		log_stacktrace(L_ERR);\
		return;\
	}

#define getCtx_chained \
	CallCtx *ctx = getCtx(call);\
	if(NULL==ctx){\
		ERROR("CallCtx = nullptr ");\
		log_stacktrace(L_ERR);\
		return ContinueProcessing;\
	}

inline Cdr *getCdr(CallCtx *ctx) { return ctx->cdr; }
inline Cdr *getCdr(SBCCallLeg *call) { return getCdr(getCtx(call)); }

inline void replace(string& s, const string& from, const string& to){
	size_t pos = 0;
	while ((pos = s.find(from, pos)) != string::npos) {
		s.replace(pos, from.length(), to);
		pos += s.length();
	}
}

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

#define with_cdr_for_read \
    Cdr *cdr = ctx->getCdrSafe<false>();\
    if(cdr)

#define with_cdr_for_write \
    Cdr *cdr = ctx->getCdrSafe<true>();\
    if(cdr)


/****************************************
 *				CC handlers				*
 ****************************************/

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

/****************************************
 * 		SBCLogicInterface handlers		*
 ****************************************/
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

/****************************************
 * 			InDialog handlers			*
 ****************************************/

bool Yeti::init(SBCCallLeg *call, const map<string, string> &values) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	CallCtx *ctx = getCtx(call);
	Cdr *cdr = getCdr(ctx);

	ctx->inc();

	SBCCallProfile &profile = call->getCallProfile();

	if(call->isALeg()){
		ostringstream ss;
		ss <<	config.msg_logger_dir << '/' <<
				call->getLocalTag() << "_" <<
				int2str(config.node_id) << ".pcap";
		profile.set_logger_path(ss.str());

		if(profile.global_tag.empty())
			profile.global_tag = call->getLocalTag();

		cdr->update_sbc(profile);

		call->setSensor(Sensors::instance()->getSensor(profile.aleg_sensor_id));

	} else {
		if(!profile.callid.empty()){
			string id = AmSession::getNewId();
			replace(profile.callid,"%uuid",id);
		}
		call->setSensor(Sensors::instance()->getSensor(profile.bleg_sensor_id));
	}
	cdr->update(*call);
	return true;
}

void Yeti::onSendRequest(SBCCallLeg *call,AmSipRequest& req, int &flags){
	bool aleg = call->isALeg();
	DBG("Yeti::onSendRequest(%p|%s) a_leg = %d",
		call,call->getLocalTag().c_str(),aleg);
}

void Yeti::onStateChange(SBCCallLeg *call, const CallLeg::StatusChangeCause &cause){
	string reason;
    getCtx_void
	SBCCallLeg::CallStatus status = call->getCallStatus();
	bool aleg = call->isALeg();
	int internal_disconnect_code = 0;

	DBG("Yeti::onStateChange(%p|%s) a_leg = %d",
		call,call->getLocalTag().c_str(),call->isALeg());

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
				/*if(aleg && status==CallLeg::Disconnected)
					cdr->update_bleg_reason(cause.param.reply->reason,
												cause.param.reply->code);*/

				reason = "SipReply. code = "+int2str(cause.param.reply->code);
				if(cause.param.reply->code==408){
					internal_disconnect_code = DC_TRANSACTION_TIMEOUT;
				}
			} else
				reason = "SipReply. empty reply";
			break;
		case CallLeg::StatusChangeCause::SipRequest:
			if(cause.param.request!=NULL){
				/*if(aleg && status==CallLeg::Disconnected)
					cdr->update_bleg_reason(cause.param.request->method,
												cause.param.request->method==SIP_METH_BYE?200:500);*/
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

	if(internal_disconnect_code && status==CallLeg::Disconnected){
		unsigned int internal_code,response_code;
		string internal_reason,response_reason;

		CodesTranslator::instance()->translate_db_code(
			internal_disconnect_code,
			internal_code,internal_reason,
			response_code,response_reason,
			ctx->getOverrideId());
        with_cdr_for_read {
            cdr->update_internal_reason(DisconnectByTS,internal_reason,internal_code);
        }
	}

	DBG("%s(%p,leg%s,state = %s, cause = %s)",FUNC_NAME,call,aleg?"A":"B",
		callStatus2str(status),
		reason.c_str());

}

void Yeti::onDestroyLeg(SBCCallLeg *call){
	DBG("%s(%p|%s,leg%s)",FUNC_NAME,
		call,call->getLocalTag().c_str(),call->isALeg()?"A":"B");
    getCtx_void
	ctx->lock();
	call->setLogicData(NULL);
	if(ctx->dec_and_test()){
		onLastLegDestroy(ctx,call);
		ctx->router->release(routers);
		ctx->unlock();
		delete ctx;
	} else {
		SqlCallProfile *p = ctx->getCurrentProfile();
		if(NULL!=p){
			rctl.put(p->resource_handler);
		}
		ctx->unlock();
	}
}

void Yeti::onLastLegDestroy(CallCtx *ctx,SBCCallLeg *call){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	Cdr *cdr = getCdr(ctx);
	if(cdr){
		cdr_list.erase(cdr);
		ctx->write_cdr(cdr,true);
	}
}

CCChainProcessing Yeti::onBLegRefused(SBCCallLeg *call, AmSipReply& reply) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    getCtx_chained
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
			//rctl.put(ctx->getCurrentResourceList());
			rctl.put(ctx->getCurrentProfile()->resource_handler);

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
		rctl_ret = rctl.get(ctx->getCurrentResourceList(),
							ctx->getCurrentProfile()->resource_handler,
							call->getLocalTag(),
							refuse_code,refuse_reason);

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

			/* show resource disconnect reason instead of
			 * refuse_profile if refuse_profile follows failed resource with
			 * failover to next */
			if(profile->disconnect_code_id==0){
				throw AmSession::Exception(refuse_code,refuse_reason);
			}

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

	SBCCallProfile &call_profile = call->getCallProfile();
	profile = ctx->getCurrentProfile();
	profile->global_tag = call_profile.global_tag; //save to old global_tag profile to new profile
	call_profile = *profile; //replace leg call_profile

	//filterSDP
	int res = negotiateRequestSdp(call_profile,
							  req,
							  ctx->aleg_negotiated_media,
							  req.method,
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

	if(!call_profile.append_headers.empty()){
		replace(call_profile.append_headers,"%global_tag",call_profile.global_tag);
	}

	} catch(InternalException &e) {
		DBG("onInitialInvite() catched InternalException(%d)",e.icode);
		rctl.put(call->getCallProfile().resource_handler);
		cdr->update_internal_reason(DisconnectByTS,e.internal_reason,e.internal_code);
		throw AmSession::Exception(e.response_code,e.response_reason);
	} catch(AmSession::Exception &e) {
		DBG("onInitialInvite() catched AmSession::Exception(%d,%s)",
			e.code,e.reason.c_str());
		rctl.put(call->getCallProfile().resource_handler);
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
    getCtx_void
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
	bool aleg = call->isALeg();

	DBG("%s(%p|%s,leg%s) '%s'",FUNC_NAME,call,call->getLocalTag().c_str(),aleg?"A":"B",req.method.c_str());

	SBCCallProfile &p = call->getCallProfile();

	if(req.method == SIP_METH_OPTIONS){
		const SBCCallProfile &p = call->getCallProfile();
		if( (aleg && !p.aleg_relay_options) || (!aleg && !p.bleg_relay_options) ){
			//answer with 200 OK and avoid relaying
			call->dlg->reply(req, 200, "OK", NULL, "", SIP_FLAGS_VERBATIM);
			return StopProcessing;
		}
	} else if(req.method == SIP_METH_PRACK && !p.relay_prack){
		call->dlg->reply(req,200, "OK", NULL, "", SIP_FLAGS_VERBATIM);
		return StopProcessing;
	} else if(req.method == SIP_METH_INVITE && !p.relay_reinvite){
		DBG("INVITE method matched. try to process locally");
        getCtx_chained

		//check for hold/unhold request to pass them transparentlyAmSdp sdp;
		AmSdp sdp;
		if(AmMimeBody2Sdp(req.body,sdp)!=0){
			return ContinueProcessing;
		}
		HoldMethod method;

		if(isHoldRequest(sdp,method)){
			DBG("hold request matched. relay_hold = %d",p.relay_hold);
			if(p.relay_hold){
				DBG("skip local processing for hold request");
				ctx->on_hold = true;
				return ContinueProcessing;
			}
		} else if(ctx->on_hold){
			DBG("we in hold state. skip local processing for unhold request");
			ctx->on_hold = false;
			return ContinueProcessing;
		}

		DBG("replying 100 Trying to INVITE to be processed locally\n");
		call->dlg->reply(req, 100, SIP_REPLY_TRYING);

		bool single_codec;
		int static_codecs_negotiate_id;
		vector<SdpMedia> * negotiated_media;

		if(aleg){
			single_codec = p.aleg_single_codec;
			static_codecs_negotiate_id = p.static_codecs_aleg_id;
			negotiated_media = &ctx->aleg_negotiated_media;
		} else {
			single_codec = p.bleg_single_codec;
			static_codecs_negotiate_id = p.static_codecs_bleg_id;
			negotiated_media = &ctx->bleg_negotiated_media;
		}

		AmSipRequest inv_req(req);

		int res = negotiateRequestSdp(p,
									  inv_req,
									  *negotiated_media,
									  inv_req.method,
									  static_codecs_negotiate_id,
									  true,
									  single_codec);
		if (res < 0) {
			throw AmSession::Exception(488,"Not Acceptable Here");
		}

		call->processLocalReInvite(inv_req);
		return StopProcessing;
	}

	if(aleg){
		if(req.method==SIP_METH_CANCEL){
            getCtx_chained
            with_cdr_for_read {
                cdr->update_internal_reason(DisconnectByORG,"Request terminated (Cancel)",487);
            }
		}
	}

	return ContinueProcessing;
}

CCChainProcessing Yeti::onInDialogReply(SBCCallLeg *call, const AmSipReply &reply) {
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    if(!call->isALeg()){
        getCtx_chained
        with_cdr_for_read {
            cdr->update(reply);
        }
	}
	return ContinueProcessing;
}

CCChainProcessing Yeti::onEvent(SBCCallLeg *call, AmEvent *e) {
	DBG("%s(%p|%s,leg%s)",FUNC_NAME,call,
		call->getLocalTag().c_str(),call->isALeg()?"A":"B");

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

	B2BEvent* b2b_e = dynamic_cast<B2BEvent*>(e);
	if(b2b_e){
		if(b2b_e->event_id==B2BTerminateLeg){
			DBG("onEvent(%p|%s) terminate leg event",
				call,call->getLocalTag().c_str());
		}
	}
	if (e->event_id == E_SYSTEM) {
		AmSystemEvent* sys_ev = dynamic_cast<AmSystemEvent*>(e);
		if(sys_ev){
			DBG("sys event type: %d",sys_ev->sys_event);
			onSystemEvent(call,sys_ev);
		}
	}

	yeti_dtmf::DtmfInfoSendEvent *dtmf = dynamic_cast<yeti_dtmf::DtmfInfoSendEvent*>(e);
	if(dtmf){
		DBG("onEvent dmtf(%d:%d)",dtmf->event(),dtmf->duration());
		dtmf->send(call->dlg);
		e->processed = true;
		return StopProcessing;
	}

	return ContinueProcessing;
}

CCChainProcessing Yeti::onDtmf(SBCCallLeg *call, AmDtmfEvent* e){
	DBG("%s(call = %p,event = %d,duration = %d)",
		FUNC_NAME,call,e->event(),e->duration());

	AmSipDtmfEvent *sip_dtmf = NULL;
	SBCCallProfile &p = call->getCallProfile();
	bool aleg = call->isALeg();
	bool allowed = false;

	//filter incoming methods
	if((sip_dtmf = dynamic_cast<AmSipDtmfEvent *>(e))){
		DBG("received SIP DTMF event\n");
		allowed = aleg ?
					p.aleg_dtmf_recv_modes&DTMF_RX_MODE_INFO :
					p.bleg_dtmf_recv_modes&DTMF_RX_MODE_INFO;
	/*} else if(dynamic_cast<AmRtpDtmfEvent *>(e)){
		DBG("RTP DTMF event\n");*/
	} else {
		DBG("received generic DTMF event\n");
		allowed = aleg ?
					p.aleg_dtmf_recv_modes&DTMF_RX_MODE_RFC2833 :
					p.bleg_dtmf_recv_modes&DTMF_RX_MODE_RFC2833;
	}

	if(!allowed){
		DBG("DTMF event for leg %p rejected",call);
		e->processed = true;
		return StopProcessing;
	}

	//choose outgoing method
	int send_method = aleg ? p.bleg_dtmf_send_mode_id : p.aleg_dtmf_send_mode_id;
	switch(send_method){
	case DTMF_TX_MODE_DISABLED:
		DBG("dtmf sending is disabled");
		return StopProcessing;
		break;
	case DTMF_TX_MODE_RFC2833:
		DBG("send mode RFC2833 choosen for dtmf event for leg %p",call);
		return ContinueProcessing; //nothing to do. it's default methos thus just continue processing
		break;
	case DTMF_TX_MODE_INFO_DTMF_RELAY:
		DBG("send mode INFO/application/dtmf-relay choosen for dtmf event for leg %p",call);
		call->relayEvent(new yeti_dtmf::DtmfInfoSendEventDtmfRelay(e));
		return StopProcessing;
		break;
	case DTMF_TX_MODE_INFO_DTMF:
		DBG("send mode INFO/application/dtmf choosen for dtmf event for leg %p",call);
		call->relayEvent(new yeti_dtmf::DtmfInfoSendEventDtmf(e));
		return StopProcessing;
		break;
	default:
		ERROR("unknown dtmf send method %d. stop processing",send_method);
		return StopProcessing;
	}
}

CCChainProcessing Yeti::onRtpTimeout(SBCCallLeg *call,const AmRtpTimeoutEvent &rtp_event){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	unsigned int internal_code,response_code;
	string internal_reason,response_reason;

    getCtx_chained

	if(call->getCallStatus()!=CallLeg::Connected){
		WARN("module catched RtpTimeout in no Connected state. ignore it");
		return StopProcessing;
	}

	CodesTranslator::instance()->translate_db_code(
		DC_RTP_TIMEOUT,
		internal_code,internal_reason,
		response_code,response_reason,
		ctx->getOverrideId());
    with_cdr_for_read {
        cdr->update_internal_reason(DisconnectByTS,internal_reason,internal_code);
        cdr->update_aleg_reason("Bye",200);
        cdr->update_bleg_reason("Bye",200);
    }
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
    getCtx_chained
    with_cdr_for_read {
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
    getCtx_void
    with_cdr_for_read {
        cdr->update_internal_reason(DisconnectByTS,"ServerShutdown",200);
    }
	//may never reach onDestroy callback so free resources here
	rctl.put(call->getCallProfile().resource_handler);
}

CCChainProcessing Yeti::onTearDown(SBCCallLeg *call){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    getCtx_chained
    with_cdr_for_read {
        cdr->update_internal_reason(DisconnectByTS,"Teardown",200);
        cdr->update_aleg_reason("Bye",200);
        cdr->update_bleg_reason("Bye",200);
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
    getCtx_chained
	if(call->isALeg()){
		//trace available values
		if(ctx->initial_invite!=NULL){
			AmSipRequest &req = *ctx->initial_invite;
			DBG("req.method = '%s'",req.method.c_str());
		} else {
			ERROR("intial_invite == NULL");
		}
        with_cdr_for_read {
            cdr->update_internal_reason(DisconnectByTS,reply.reason,reply.code);
        }
	}
	return ContinueProcessing;
}

CCChainProcessing Yeti::onBye(SBCCallLeg *call, const AmSipRequest &req){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    getCtx_chained
    with_cdr_for_read {
        if(call->isALeg()){
            if(call->getCallStatus()!=CallLeg::Connected){
                ERROR("received Bye in not connected state");
                cdr->update_internal_reason(DisconnectByORG,"EarlyBye",500);
                cdr->update_aleg_reason("EarlyBye",200);
                cdr->update_bleg_reason("Cancel",487);
            } else {
                cdr->update_internal_reason(DisconnectByORG,"Bye",200);
                cdr->update_bleg_reason("Bye",200);
            }
        } else {
            cdr->update_internal_reason(DisconnectByDST,"Bye",200);
            cdr->update_bleg_reason("Bye",200);
        }
    }
	return ContinueProcessing;
}

CCChainProcessing Yeti::onOtherBye(SBCCallLeg *call, const AmSipRequest &req){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    getCtx_chained
	if(call->isALeg()){
		if(call->getCallStatus()!=CallLeg::Connected){
			//avoid considering of bye in not connected state as succ call
			ERROR("received OtherBye in not connected state");
            with_cdr_for_write {
				cdr->update_internal_reason(DisconnectByDST,"EarlyBye",500);
				cdr->update_aleg_reason("Request terminated",487);
				cdr_list.erase(cdr);
				ctx->write_cdr(cdr,true);
			}
		}
	}
	return ContinueProcessing;
}

void Yeti::onCallConnected(SBCCallLeg *call, const AmSipReply& reply){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    getCtx_void
	if(call->isALeg()){
		Cdr *cdr = getCdr(ctx);
		cdr->update(Connect);
	}
}

void Yeti::onCallEnded(SBCCallLeg *call){
    getCtx_void
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
	if(call->isALeg()){
        with_cdr_for_read {
            cdr->update(End);
            cdr_list.erase(cdr);
        }
	}
}

void Yeti::onRTPStreamDestroy(SBCCallLeg *call,AmRtpStream *stream){
	DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    getCtx_void
	getCdr(ctx)->update(call,stream);
}

static void copyMediaPayloads(vector<SdpMedia> &dst, const vector<SdpMedia> &src){
	if(src.empty()){
		DBG("still no negotiated media. skip copy");
		return;
	}

	if(dst.size()!=src.size()){
		ERROR("received and negotiated media have different streams count");
		return;
	}

	vector<SdpMedia>::iterator i = dst.begin();
	for (vector<SdpMedia>::const_iterator j = src.begin(); j != src.end(); ++j, ++i) {
		i->payloads = j->payloads;
		/*i->recv = j->recv;
		i->send = j->send;*/
	}
}

void Yeti::onSdpCompleted(SBCCallLeg *call, AmSdp& offer, AmSdp& answer){
	bool aleg = call->isALeg();

	DBG("%s(%p,leg%s)",FUNC_NAME,call,aleg?"A":"B");

    getCtx_void
	const vector<SdpMedia> &negotiated_media = aleg ?
				ctx->aleg_negotiated_media :
				ctx->bleg_negotiated_media;

	dump_SdpMedia(negotiated_media,"negotiated media");

	//fix sdp for relay mask computing
	copyMediaPayloads(answer.media,negotiated_media);

	dump_SdpMedia(offer.media,"offer");
	dump_SdpMedia(answer.media,"answer");
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
			vector<SdpMedia> * negotiated_media;

			if(a_leg){
				static_codecs_negotiate_id = call_profile.static_codecs_aleg_id;
				static_codecs_filter_id = call_profile.static_codecs_bleg_id;
				negotiated_media = &ctx->aleg_negotiated_media;
			} else {
				static_codecs_negotiate_id = call_profile.static_codecs_bleg_id;
				static_codecs_filter_id = call_profile.static_codecs_aleg_id;
				negotiated_media = &ctx->bleg_negotiated_media;
			}

			int res = negotiateRequestSdp(call_profile,
										  req_ev->req,
										  *negotiated_media,
										  req_ev->req.method,
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

			SBCCallProfile &call_profile = call->getCallProfile();
			vector<SdpMedia> * negotiated_media;
			bool single_codec;
			if(a_leg){
				negotiated_media = &ctx->bleg_negotiated_media;
				single_codec = call_profile.bleg_single_codec;
			} else {
				negotiated_media = &ctx->aleg_negotiated_media;
				single_codec = call_profile.aleg_single_codec;
			}

			filterReplySdp(call,
						   reply_ev->reply.body, reply_ev->reply.cseq_method,
						   *negotiated_media,
						   single_codec,
						   call_profile.filter_noaudio_streams);

			//append headers for 200 OK replyin direction B -> A
			AmSipReply &reply = reply_ev->reply;
			if(!a_leg &&
				reply.code==200 &&
				!call_profile.aleg_append_headers_reply.empty()){

				size_t start_pos = 0;
				while (start_pos<call_profile.aleg_append_headers_reply.length()) {
					int res;
					size_t name_end, val_begin, val_end, hdr_end;
					if ((res = skip_header(call_profile.aleg_append_headers_reply, start_pos, name_end, val_begin,
							val_end, hdr_end)) != 0) {
						ERROR("skip_header for '%s' pos: %ld, return %d",
								call_profile.aleg_append_headers_reply.c_str(),start_pos,res);
						throw AmSession::Exception(500, SIP_REPLY_SERVER_INTERNAL_ERROR);
					}
					string hdr_name = call_profile.aleg_append_headers_reply.substr(start_pos, name_end-start_pos);
					start_pos = hdr_end;
					while(!getHeader(reply.hdrs,hdr_name).empty()){
						removeHeader(reply.hdrs,hdr_name);
					}
				}

				reply.hdrs+=call_profile.aleg_append_headers_reply;
			}
		} break;
		case B2BDtmfEvent:
			//
			break;
	} //switch(e->event_id)
	return 0;
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

void Yeti::onSipRequest(const AmSipRequest& req, void *user_data) {
	DBG("%s() called",FUNC_NAME);
}

/****************************************
 * 			OoD handlers				*
 ****************************************/

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

/****************************************
 *				aux funcs				*
 ****************************************/

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
	if(!call_profile.append_headers.empty()){
		replace(call_profile.append_headers,"%global_tag",call_profile.global_tag);
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
	ctx->write_cdr(cdr,false);
	cdr = getCdr(ctx);

	do {
		DBG("%s() choosed next profile. check it for refuse",FUNC_NAME);

		ParamReplacerCtx rctx(profile);
		if(check_and_refuse(profile,cdr,*ctx->initial_invite,rctx)){
			DBG("%s() profile contains refuse code",FUNC_NAME);
			break;
		}

		DBG("%s() no refuse field. check it for resources",FUNC_NAME);
        ResourceList &rl = profile->rl;
		if(rl.empty()){
			rctl_ret = RES_CTL_OK;
		} else {
			rctl_ret = rctl.get(rl,
								profile->resource_handler,
								call->getLocalTag(),
								refuse_code,refuse_reason);
		}

		if(rctl_ret == RES_CTL_OK){
			DBG("%s() check resources  successed",FUNC_NAME);
            //profile = ctx->getCurrentProfile();
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
			ctx->write_cdr(cdr,true);
			cdr = getCdr(ctx);

			if(NULL==profile){
				DBG("%s() there are no profiles more",FUNC_NAME);
				break;
			}

			if(profile->disconnect_code_id!=0){
				/* use code and reason from resource check
				 * instead of refuse code of profile
				 * if we see refuse_profile after failed resource check with
				 * failover to next */
				break;
			}
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
