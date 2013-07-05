#ifndef _SBC_LOGIC_INTERFACE_H
#define _SBC_LOGIC_INTERFACE_H

#include "AmSipMsg.h"
#include "ParamReplacer.h"

enum getProfileRequestType {
    InDialogRequest,
    OutOfDialogRequest
};

class SBCLogicInterface
{
  public:
    /**
     * @brief getCallProfile
     * @param RequestType helps to distinguish requests from onInvite() and onOoDRequest()
     * @return reference to already cloned, ready to use CallProfile
     */
    virtual SBCCallProfile& getCallProfile( const AmSipRequest& req,
                                            ParamReplacerCtx& ctx,
                                            getProfileRequestType RequestType ) = 0;
};

#endif //_SBC_LOGIC_INTERFACE_H
