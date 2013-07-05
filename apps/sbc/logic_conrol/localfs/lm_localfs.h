#ifndef _LM_LOCALFS_H
#define _LM_LOCALFS_H

#include "AmApi.h"
#include "AmConfigReader.h"
#include "SBCCallProfile.h"
#include "SBCLogicInterface.h"

class LMLocalFS : public AmDynInvoke, SBCLogicInterface
{
  static LMLocalFS* _instance;

  std::map<string, SBCCallProfile> call_profiles;
  vector<string> active_profile;
  AmMutex profiles_mut;

  AmConfigReader cfg;

  void listProfiles(const AmArg& args, AmArg& ret);
  void reloadProfiles(const AmArg& args, AmArg& ret);
  void reloadProfile(const AmArg& args, AmArg& ret);
  void loadProfile(const AmArg& args, AmArg& ret);
  void getActiveProfile(const AmArg& args, AmArg& ret);
  void setActiveProfile(const AmArg& args, AmArg& ret);

  /** get the first matching profile name from active profiles */
  SBCCallProfile* getActiveProfileMatch(const AmSipRequest& req,
                    ParamReplacerCtx& ctx);
 public:
  LMLocalFS();
  ~LMLocalFS();
  static LMLocalFS* instance();
  void invoke(const string& method, const AmArg& args, AmArg& ret);
  int onLoad();

  SBCCallProfile& getCallProfile( const AmSipRequest& req,
                                          ParamReplacerCtx& ctx,
                                          getProfileRequestType RequestType );

};

#endif 
