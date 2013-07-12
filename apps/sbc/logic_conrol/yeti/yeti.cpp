#include "yeti.h"

#include "log.h"
#include "AmPlugIn.h"
#include "AmArg.h"
#include "AmSession.h"
#include "CallLeg.h"

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
    router(new SqlRouter()),
    writer(new CdrWriter())
{
    DBG("Yeti()");
}


Yeti::~Yeti() {
    DBG("~Yeti()");
    router->stop();
}

int Yeti::onLoad() {
    if(cfg.loadFile(AmConfig::ModConfigPath + string(MOD_NAME ".conf"))) {
      ERROR("No configuration for "MOD_NAME" present (%s)\n",
       (AmConfig::ModConfigPath + string(MOD_NAME ".conf")).c_str()
      );
      return -1;
    }

    self_iface.cc_module = "yeti";
    self_iface.cc_name = "yeti";
    self_iface.cc_values.clear();

    profile = new SBCCallProfile();
    string profile_file_name = AmConfig::ModConfigPath + "transparent.sbcprofile.conf";
    profile->readFromConfiguration("transparent",profile_file_name);
    profile->cc_vars.clear();
    profile->cc_interfaces.clear();
    profile->cc_interfaces.push_back(self_iface);   //add reference to ourself

    DBG("p = %p",profile);

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
  } else if(method == "_list"){
    ret.push("getLogicInterfaceHandler");
  }
  else
    throw AmDynInvoke::NotImplemented(method);
}


SBCCallProfile& Yeti::getCallProfile( const AmSipRequest& req,
                                        ParamReplacerCtx& ctx,
                                        getProfileRequestType RequestType )
{
    SqlCallProfile *profile;
    DBG("%s() called",FUNC_NAME);
    if(RequestType == OutOfDialogRequest){
        DBG("Unsupported request method '%s'",req.method.c_str());

        //AmSipDialog::reply_error(req,404,"Not found");

        profile = new SqlCallProfile();
        profile->refuse_with = "400 Unsupported method";

    } else {
        profile = router->getprofile(req);
    }
    DBG("%s() profile = %p",FUNC_NAME,profile);
    profile->cc_interfaces.push_back(self_iface);
    //profile->ref_cnt.inc();

    SqlCallProfile &p = *profile;

    DBG("%s() return = %p",FUNC_NAME,&p);
    return p;
}

void Yeti::onRefuseRequest(SBCCallProfile *call_profile){
    SqlCallProfile *profile = dynamic_cast<SqlCallProfile *>(call_profile);
    DBG("%s(%p)",FUNC_NAME,call_profile);
    if(profile){
        DBG("%s(%p) profile->cached = %d",FUNC_NAME,call_profile,profile->cached);
//        if(profile->ref_cnt.dec_and_test())
  //          if(!profile->cached)
                delete profile;
    }
}

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
        //if(call_profile->ref_cnt.dec_and_test())
            delete call_profile;
    }
}

void Yeti::init(SBCCallLeg *call, const map<string, string> &values) {
    DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
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
}

CCChainProcessing Yeti::onBLegRefused(SBCCallLeg *call, const AmSipReply& reply) {
    DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    return ContinueProcessing;
}

CCChainProcessing Yeti::onInitialInvite(SBCCallLeg *call, InitialInviteHandlerParams &params) {
    DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    return ContinueProcessing;
}

CCChainProcessing Yeti::onInDialogRequest(SBCCallLeg *call, const AmSipRequest &req) {
    DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    return ContinueProcessing;
}

CCChainProcessing Yeti::onInDialogReply(SBCCallLeg *call, const AmSipReply &reply) {
    DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
    return ContinueProcessing;
}

CCChainProcessing Yeti::onEvent(SBCCallLeg *call, AmEvent *e) {
    DBG("%s(%p,leg%s)",FUNC_NAME,call,call->isALeg()?"A":"B");
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
