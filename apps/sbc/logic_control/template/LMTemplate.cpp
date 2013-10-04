
#include "AmPlugIn.h"
#include "log.h"
#include "AmArg.h"

#include "LMTemplate.h"
#include "AmSession.h"
#include <string.h>

class LMTemplateFactory : public AmDynInvokeFactory
{
public:
    LMTemplateFactory(const string& name)
	: AmDynInvokeFactory(name) {}

    AmDynInvoke* getInstance(){
    return LMTemplate::instance();
    }

    int onLoad(){
      if (LMTemplate::instance()->onLoad())
	return -1;

      DBG("template logic control loaded.\n");

      return 0;
    }
};

EXPORT_PLUGIN_CLASS_FACTORY(LMTemplateFactory, MOD_NAME);

LMTemplate* LMTemplate::_instance=0;

LMTemplate* LMTemplate::instance()
{
    if(!_instance)
    _instance = new LMTemplate();
    return _instance;
}

LMTemplate::LMTemplate()
{
}

LMTemplate::~LMTemplate() { }

int LMTemplate::onLoad() {
  return 0;
}

void LMTemplate::invoke(const string& method, const AmArg& args, AmArg& ret)
{
  DBG("LMTemplate: %s(%s)\n", method.c_str(), AmArg::print(args).c_str());

  if(method == "getLogicInterfaceHandler"){
    SBCLogicInterface *iface = this;
    ret[0] = (AmObject *)iface;
  } else if(method == "_list"){
    ret.push("getLogicInterfaceHandler");
  }
  else
    throw AmDynInvoke::NotImplemented(method);
}


SBCCallProfile& LMTemplate::getCallProfile( const AmSipRequest& req,
                                        ParamReplacerCtx& ctx,
                                        getProfileRequestType RequestType )
{
    DBG("template getCallProfile stub");
    throw AmSession::Exception(500,SIP_REPLY_SERVER_INTERNAL_ERROR);
}
