#ifndef CALLCTX_H
#define CALLCTX_H

#include <list>

#include "Cdr.h"
#include "SqlCallProfile.h"
#include "Resource.h"

struct CallCtx: public
	atomic_int
{
	Cdr *cdr;
	list<SqlCallProfile *> profiles;
	list<SqlCallProfile *>::iterator current_profile;
	int attempt_num;
	SqlCallProfile *getFirstProfile();
	SqlCallProfile *getNextProfile(bool early_state);
	SqlCallProfile *getCurrentProfile();
	ResourceList &getCurrentResourceList();

	CallCtx();
	~CallCtx();
};

#endif // CALLCTX_H
