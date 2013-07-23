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

class Yeti : public AmDynInvoke, AmObject, SBCLogicInterface, ExtendedCCInterface
{
  static Yeti* _instance;

  SBCCallProfile *profile;  //profile for OoD requests
  CCInterface self_iface;
  CdrList cdr_list;
  SqlRouter *router;

  AmConfigReader cfg;
  //config values
  int calls_show_limit;

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
  CCChainProcessing onBLegRefused(SBCCallLeg *call, const AmSipReply& reply);

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
