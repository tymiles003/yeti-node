#ifndef _LM_YETI_H
#define _LM_YETI_H

#include "AmApi.h"
#include "SBCCallProfile.h"
#include "SBCLogicInterface.h"
#include "SBCCallLeg.h"

#include "SBCCallControlAPI.h"
#include "ExtendedCCInterface.h"

#include "SqlCallProfile.h"
#include "cdr/Cdr.h"
#include "SqlRouter.h"
#include "hash/CdrList.h"
#include "cdr/CdrWriter.h"
#include "SBC.h"
#include "resources/ResourceControl.h"
#include "CallCtx.h"
#include "CodesTranslator.h"
#include "CodecsGroup.h"

#include <ctime>

#define YETI_ENABLE_PROFILING 1

#define YETI_CALL_DURATION_TIMER SBC_TIMER_ID_CALL_TIMERS_START
#define YETI_RINGING_TIMEOUT_TIMER (SBC_TIMER_ID_CALL_TIMERS_START+1)

#if YETI_ENABLE_PROFILING

#define PROF_START(var) timeval prof_start_##var; gettimeofday(&prof_start_##var,NULL);
#define PROF_END(var) timeval prof_end_##var; gettimeofday(&prof_end_##var,NULL);
#define PROF_DIFF(var) timeval prof_diff_##var; timersub(&prof_end_##var,&prof_start_##var,&prof_diff_##var);
#define PROF_PRINT(descr,var) PROF_DIFF(var); DBG(descr" took %f",prof_diff_##var.tv_sec+prof_diff_##var.tv_usec/1e6);

#else

#define PROF_START(var) ;
#define PROF_END(var) ;
#define PROF_DIFF(var) (-1)
#define PROF_PRINT(descr,var) ;

#endif

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
  set<SqlRouter *> routers;
  AmMutex router_mutex;
  AmArg xmlrpc_cmds;
  AmConfigReader cfg;
  //config values
  int calls_show_limit;

  void onLastLegDestroy(CallCtx *ctx,SBCCallLeg *call);
  /*! create new B leg (serial fork)*/
  /*! choose next profile, create cdr and check resources */
  bool connectCallee(SBCCallLeg *call,const AmSipRequest &orig_req);
  bool chooseNextProfile(SBCCallLeg *call);
  /*! return true if call refused */
  bool check_and_refuse(SqlCallProfile *profile,Cdr *cdr,
						const AmSipRequest& req,ParamReplacerCtx& ctx,
						bool send_reply = false);

  bool read_config();

  CCChainProcessing onRtpTimeout(SBCCallLeg *call,const AmRtpTimeoutEvent &rtp_event);
  void onServerShutdown(SBCCallLeg *call);
  CCChainProcessing onControlEvent(SBCCallLeg *call,SBCControlEvent *event);
  CCChainProcessing onSystemEvent(SBCCallLeg *call,AmSystemEvent* event);
  CCChainProcessing onTimerEvent(SBCCallLeg *call,int timer_id);
  CCChainProcessing onTearDown(SBCCallLeg *call);

 public:
  Yeti();
  ~Yeti();
  static Yeti* instance();
  void invoke(const string& method, const AmArg& args, AmArg& ret);
  int onLoad();
  void init_xmlrpc_cmds();
  void process_xmlrpc_cmds(const AmArg cmds_tree, const string& method, const AmArg& args, AmArg& ret);

  struct global_config {
	int node_id;
	int pop_id;
	string routing_schema;
	string msg_logger_dir;
  } config;

  time_t start_time;

		  //!xmlrpc handlers
  typedef void xmlrpc_handler(const AmArg& args, AmArg& ret);

  xmlrpc_handler DropCall;
  xmlrpc_handler ClearStats;
  xmlrpc_handler ClearCache;
  xmlrpc_handler ShowCache;
  xmlrpc_handler GetStats;
  xmlrpc_handler GetConfig;
  xmlrpc_handler GetCall;
  xmlrpc_handler GetCalls;
  xmlrpc_handler GetCallsFields;
  xmlrpc_handler GetCallsCount;
  xmlrpc_handler GetRegistration;
  xmlrpc_handler RenewRegistration;
  xmlrpc_handler GetRegistrations;
  xmlrpc_handler GetRegistrationsCount;
  xmlrpc_handler showVersion;
  xmlrpc_handler closeCdrFiles;
  xmlrpc_handler reload;
  xmlrpc_handler reloadResources;
  xmlrpc_handler reloadTranslations;
  xmlrpc_handler reloadRegistrations;
  xmlrpc_handler reloadCodecsGroups;
  xmlrpc_handler reloadRouter;
  xmlrpc_handler showMediaStreams;
  xmlrpc_handler showPayloads;
  xmlrpc_handler showInterfaces;
  xmlrpc_handler showRouterCdrWriterOpenedFiles;
  xmlrpc_handler requestSystemLogDump;

  xmlrpc_handler showSystemLogLevel;
  xmlrpc_handler setSystemLogSyslogLevel;
  xmlrpc_handler setSystemLogDiLogLevel;

  xmlrpc_handler showSessions;
  xmlrpc_handler setSessionsLimit;

  xmlrpc_handler requestSystemShutdown;
  xmlrpc_handler requestSystemShutdownImmediate;
  xmlrpc_handler requestSystemShutdownGraceful;
  xmlrpc_handler requestSystemShutdownCancel;

  xmlrpc_handler showSystemStatus;
  xmlrpc_handler showSystemAlarms;

  xmlrpc_handler getResourceState;
  xmlrpc_handler showResources;
  xmlrpc_handler showResourceTypes;
  xmlrpc_handler showResourceByHandler;
  xmlrpc_handler showResourceByLocalTag;
  xmlrpc_handler requestResourcesInvalidate;

  xmlrpc_handler requestResolverClear;
  xmlrpc_handler requestResolverGet;

  bool reload_config(AmArg &ret);
  bool check_event_id(int event_id, AmArg &ret);
  bool assert_event_id(const AmArg &args,AmArg &ret);

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
  bool init(SBCCallLeg *call, const map<string, string> &values);

  void onSendRequest(SBCCallLeg *call,AmSipRequest& req, int &flags);
  void onStateChange(SBCCallLeg *call, const CallLeg::StatusChangeCause &cause);
  void onDestroyLeg(SBCCallLeg *call);
  CCChainProcessing onBLegRefused(SBCCallLeg *call,AmSipReply& reply);

  CCChainProcessing onInitialInvite(SBCCallLeg *call, InitialInviteHandlerParams &params);
  void onInviteException(SBCCallLeg *call,int code,string reason,bool no_reply);
  CCChainProcessing onInDialogRequest(SBCCallLeg *call, const AmSipRequest &req);
  CCChainProcessing onInDialogReply(SBCCallLeg *call, const AmSipReply &reply);
  CCChainProcessing onEvent(SBCCallLeg *call, AmEvent *e);
  CCChainProcessing onDtmf(SBCCallLeg *call, int event, int duration);
  CCChainProcessing putOnHold(SBCCallLeg *call);
  CCChainProcessing resumeHeld(SBCCallLeg *call, bool send_reinvite);
  CCChainProcessing createHoldRequest(SBCCallLeg *call, AmSdp &sdp);
  CCChainProcessing handleHoldReply(SBCCallLeg *call, bool succeeded);

  CCChainProcessing onRemoteDisappeared(SBCCallLeg *call, const AmSipReply &reply);
  CCChainProcessing onBye(SBCCallLeg *call, const AmSipRequest &req);
  CCChainProcessing onOtherBye(SBCCallLeg *call, const AmSipRequest &req);
  void onCallConnected(SBCCallLeg *call, const AmSipReply& reply);
  void onCallEnded(SBCCallLeg *call);

  void onRTPStreamDestroy(SBCCallLeg *call,AmRtpStream *stream);
  void onSdpCompleted(SBCCallLeg *call, AmSdp& offer, AmSdp& answer);

  int relayEvent(SBCCallLeg *call, AmEvent *e);

        //!OoD handlers
  bool init(SBCCallProfile &profile, SimpleRelayDialog *relay, void *&user_data);
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
