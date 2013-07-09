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

    AmDynInvoke* getInstance(){
    return Yeti::instance();
    }

    int onLoad(){
      if (Yeti::instance()->onLoad())
	return -1;

      DBG("template logic control loaded.\n");

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

Yeti::Yeti()
{
}

Yeti::~Yeti() { }

int Yeti::onLoad() {

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
  } else if(method == "start"){ //!stubs to bypass exception
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
    DBG("%s() called",FUNC_NAME);
    SBCCallProfile &p = *profile;
    return p;
}


void Yeti::start(const string& cc_name, const string& ltag,
               SBCCallProfile* call_profile,
               int start_ts_sec, int start_ts_usec,
               const AmArg& values, int timer_id, AmArg& res) {
    DBG("%s(%p,%s)",FUNC_NAME,call_profile,ltag.c_str());
    res.push(AmArg());

  //AmArg& res_cmd = res[0];

  // Drop:
  // res_cmd[SBC_CC_ACTION] = SBC_CC_DROP_ACTION;

  // res_cmd[SBC_CC_ACTION] = SBC_CC_REFUSE_ACTION;
  // res_cmd[SBC_CC_REFUSE_CODE] = 404;
  // res_cmd[SBC_CC_REFUSE_REASON] = "No, not here";

  // Set Timer:
  // DBG("my timer ID will be %i\n", timer_id);
  // res_cmd[SBC_CC_ACTION] = SBC_CC_SET_CALL_TIMER_ACTION;
  // res_cmd[SBC_CC_TIMER_TIMEOUT] = 5;
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
