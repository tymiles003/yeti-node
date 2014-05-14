#ifndef CALLCTX_H
#define CALLCTX_H

#include <list>

#include "cdr/Cdr.h"
#include "SqlCallProfile.h"
#include "resources/Resource.h"
#include "SqlRouter.h"
class SqlRouter;

struct CallCtx: public
	atomic_int
{
	Cdr *cdr;
	bool cdr_processed;
	list<SqlCallProfile *> profiles;
	list<SqlCallProfile *>::iterator current_profile;
	int attempt_num;
	AmSipRequest *initial_invite;
	vector<SdpMedia> aleg_negotiated_media;
	vector<SdpMedia> bleg_negotiated_media;
	SqlRouter *router;
	bool SQLexception;

	SqlCallProfile *getFirstProfile();
	SqlCallProfile *getNextProfile(bool early_state);
	SqlCallProfile *getCurrentProfile();
	SqlRouter *getRouter();
	ResourceList &getCurrentResourceList();
	int getOverrideId(bool aleg = true);
	void setCdrProcessed();

	CallCtx(SqlRouter *router);
	~CallCtx();
};

CallCtx *getCtx(SBCCallLeg *call);

#endif // CALLCTX_H
