#include "dtmf_sip_info.h"
#include "sip/defs.h"

#include "sstream"

#include "AmUtils.h"
#include "AmMimeBody.h"

namespace yeti_dtmf {

template <>
void send_dtmf<DTMF>(AmSipDialog *dlg, AmDtmfEvent* dtmf){
	AmMimeBody body;
	AmContentType type;
	string payload(int2str(dtmf->event()));

	type.setType("application");
	type.setSubType("dtmf");

	body.setContentType(type);
	body.setPayload((const unsigned char *)payload.c_str(),payload.length());

	dlg->sendRequest(SIP_METH_INFO,&body);
}

template <>
void send_dtmf<DTMF_RELAY>(AmSipDialog *dlg, AmDtmfEvent* dtmf){
	AmMimeBody body;
	AmContentType type;

	std::stringstream s;
	s << "Signal=" << dtmf->event() << std::endl;
	s << "Duration=" << dtmf->duration() << std::endl;

	string payload(s.str());

	type.setType("application");
	type.setSubType("dtmf-relay");

	body.setContentType(type);
	body.setPayload((const unsigned char *)payload.c_str(),payload.length());

	dlg->sendRequest(SIP_METH_INFO,&body);
}

void DtmfInfoSendEvent::send(AmSipDialog *dlg){
	switch(m_type){
	case DTMF:
		send_dtmf<DTMF>(dlg,this);
		break;
	case DTMF_RELAY:
		send_dtmf<DTMF_RELAY>(dlg,this);
		break;
	default:
		ERROR("unknown dtmf sip info event type");
	}
}

} //namespace yeti_dtmf

