#ifndef CALLCTX_H
#define CALLCTX_H

#include <list>

#include "Cdr.h"
#include "SqlCallProfile.h"
#include "Resource.h"
#include "SqlRouter.h"
class SqlRouter;

struct CallCtx: public
	atomic_int
{
	Cdr *cdr;
	list<SqlCallProfile *> profiles;
	list<SqlCallProfile *>::iterator current_profile;
	int attempt_num;
	AmSipRequest *initial_invite;
	SqlRouter *router;
	bool SQLexception;

	SqlCallProfile *getFirstProfile();
	SqlCallProfile *getNextProfile(bool early_state);
	SqlCallProfile *getCurrentProfile();
	SqlRouter *getRouter();
	ResourceList &getCurrentResourceList();

	CallCtx(SqlRouter *router);
	~CallCtx();
};

#endif // CALLCTX_H
