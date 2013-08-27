#ifndef _LM_YETI_H
#define _LM_YETI_H

#include "AmApi.h"
#include "SBCCallProfile.h"
#include "SBCLogicInterface.h"
#include "SBCCallLeg.h"

#include "SBCCallControlAPI.h"
#include "ExtendedCCInterface.h"

#include "SqlCallProfile.h"
#include "Cdr.h"
#include "SqlRouter.h"
#include "CdrList.h"
#include "CdrWriter.h"
#include "SBC.h"
#include "ResourceControl.h"
#include "CallCtx.h"
#include "CodesTranslator.h"

class Yeti : public AmDynInvoke, AmObject, SBCLogicInterface, ExtendedCCInterface
{
  static Yeti* _instance;

  struct RefuseException {
	  int internal_code,response_code;
	  string internal_reason,response_reason;
	  RefuseException(int ic,string ir,int rc,string rr) :
		  internal_code(ic),internal_reason(ir),
		  response_code(rc),response_reason(rr){}
  };

  SBCCallProfile *profile;  //profile for OoD requests
  CCInterface self_iface;
  CdrList cdr_list;
  ResourceControl rctl;
  SqlRouter *router;

  AmConfigReader cfg;
  //config values
  int calls_show_limit;
  CallCtx *getCtx(SBCCallLeg *call){ return reinterpret_cast<CallCtx *>(call->getLogicData()); }
  Cdr *getCdr(CallCtx *ctx) { return ctx->cdr; }
  Cdr *getCdr(SBCCallLeg *call) { return getCdr(getCtx(call)); }
  void replace(string& s, const string& from, const string& to);

  void onLastLegDestroy(CallCtx *ctx,SBCCallLeg *call);
  /*! create new B leg (serial fork)*/
  bool connectCallee(SBCCallLeg *call,const AmSipRequest &orig_req);
  /*! choose next profile, create cdr and check resources */
  bool chooseNextProfile(SBCCallLeg *call);
  /*! return true if call refused */
  bool check_and_refuse(SqlCallProfile *profile,Cdr *cdr,
						const AmSipRequest& req,ParamReplacerCtx& ctx,
						bool send_reply = false);

 public:
  Yeti();
  ~Yeti();
  static Yeti* instance();
  void invoke(const string& method, const AmArg& args, AmArg& ret);
  int onLoad();

        //!xmlrpc handlers
  void DropCall(const AmArg& args, AmArg& ret);
  void ClearStats(const AmArg& args, AmArg& ret);
  void GetStats(const AmArg& args, AmArg& ret);
  void GetConfig(const AmArg& args, AmArg& ret);
  void GetCall(const AmArg& args, AmArg& ret);
  void GetCalls(const AmArg& args, AmArg& ret);
  void GetCallsCount(const AmArg& args, AmArg& ret);
  void showVersion(const AmArg& args, AmArg& ret);

        //!SBCLogicInterface handlers
  SBCCallProfile& getCallProfile( const AmSipRequest& req,
                                          ParamReplacerCtx& ctx );

  SBCCallLeg *getCallLeg( const AmSipRequest& req,
                          ParamReplacerCtx& ctx,
                          CallLegCreator *leg_creator );

        //!CCInterface handlers
  void start(const string& cc_name, const string& ltag, SBCCallProfile* call_profile,
         int start_ts_sec, int start_ts_usec, const AmArg& values,
         int timer_id, AmArg& res);
  void connect(const string& cc_name, const string& ltag, SBCCallProfile* call_profile,
           const string& other_ltag,
           int connect_ts_sec, int connect_ts_usec);
  void end(const string& cc_name, const string& ltag, SBCCallProfile* call_profile,
       int end_ts_sec, int end_ts_usec);
  void oodHandlingTerminated(const AmSipRequest *req,SqlCallProfile *call_profile);

        //!ExtendedCCInterface handlers
  void init(SBCCallLeg *call, const map<string, string> &values);

  void onStateChange(SBCCallLeg *call);
  void onDestroyLeg(SBCCallLeg *call);
  CCChainProcessing onBLegRefused(SBCCallLeg *call,AmSipReply& reply);

  CCChainProcessing onInitialInvite(SBCCallLeg *call, InitialInviteHandlerParams &params);
  CCChainProcessing onInDialogRequest(SBCCallLeg *call, const AmSipRequest &req);
  CCChainProcessing onInDialogReply(SBCCallLeg *call, const AmSipReply &reply);
  CCChainProcessing onEvent(SBCCallLeg *call, AmEvent *e);
  CCChainProcessing putOnHold(SBCCallLeg *call);
  CCChainProcessing resumeHeld(SBCCallLeg *call, bool send_reinvite);
  CCChainProcessing createHoldRequest(SBCCallLeg *call, AmSdp &sdp);
  CCChainProcessing handleHoldReply(SBCCallLeg *call, bool succeeded);

  CCChainProcessing onRemoteDisappeared(SBCCallLeg *call, const AmSipReply &reply);
  CCChainProcessing onBye(SBCCallLeg *call, const AmSipRequest &req);
  CCChainProcessing onOtherBye(SBCCallLeg *call, const AmSipRequest &req);
  void onCallConnected(SBCCallLeg *call, const AmSipReply& reply);

        //!OoD handlers
  void init(SBCCallProfile &profile, SimpleRelayDialog *relay, void *&user_data);
  void initUAC(const AmSipRequest &req, void *user_data);
  void initUAS(const AmSipRequest &req, void *user_data);
  void finalize(void *user_data);
  void onSipRequest(const AmSipRequest& req, void *user_data);
  void onSipReply(const AmSipRequest& req,
        const AmSipReply& reply,
        AmBasicSipDialog::Status old_dlg_status,
                void *user_data);
  void onB2BRequest(const AmSipRequest& req, void *user_data);
  void onB2BReply(const AmSipReply& reply, void *user_data);
};

#endif 
