#ifndef _LM_TEMPLATE_H
#define _LM_TEMPLATE_H

#include "AmApi.h"
#include "SBCCallProfile.h"
#include "SBCLogicInterface.h"

class LMTemplate : public AmDynInvoke, SBCLogicInterface
{
  static LMTemplate* _instance;

 public:
  LMTemplate();
  ~LMTemplate();
  static LMTemplate* instance();
  void invoke(const string& method, const AmArg& args, AmArg& ret);
  int onLoad();

  SBCCallProfile& getCallProfile( const AmSipRequest& req,
                                          ParamReplacerCtx& ctx,
                                          getProfileRequestType RequestType );
};

#endif 
