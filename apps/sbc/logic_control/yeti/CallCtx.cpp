#include "CallCtx.h"

SqlCallProfile *CallCtx::getFirstProfile(){
	DBG("%s() this = %p",FUNC_NAME,this);
	if(profiles.empty())
		return NULL;
	current_profile = profiles.begin();
	attempt_num = 0;
	cdr = new Cdr(**current_profile);
	return *current_profile;
}

/*
 *we should not change the cdr or increase the number of attempts in early_state
 */
SqlCallProfile *CallCtx::getNextProfile(bool early_state){
	list<SqlCallProfile *>::iterator next_profile;
	DBG("%s() this = %p",FUNC_NAME,this);

	next_profile = current_profile;
	++next_profile;
	if(next_profile == profiles.end()){
		return NULL;
	}
	if(!early_state){
		if((*next_profile)->disconnect_code_id!=0){
			//ignore refuse profiles for non early state
			return NULL;
		}
		attempt_num++;
		cdr = new Cdr(*cdr,**next_profile);
	}
	current_profile = next_profile;
	return *current_profile;
}

SqlCallProfile *CallCtx::getCurrentProfile(){
	if(current_profile == profiles.end())
		return NULL;
	return *current_profile;
}

SqlRouter *CallCtx::getRouter(){
	return router;
}

ResourceList &CallCtx::getCurrentResourceList(){
	if(current_profile == profiles.end())
		throw AmSession::Exception(500, SIP_REPLY_SERVER_INTERNAL_ERROR);
	return (*current_profile)->rl;
}

CallCtx::CallCtx(SqlRouter *router):
	initial_invite(NULL),
	cdr(NULL),
	router(router),
	SQLexception(false)
{
	router->inc();
	DBG("%s() this = %p",FUNC_NAME,this);
}

CallCtx::~CallCtx(){
	DBG("%s() this = %p",FUNC_NAME,this);
	list<SqlCallProfile *>::iterator it = profiles.begin();
	for(;it != profiles.end();++it){
		delete (*it);
	}
	if(initial_invite)
		delete initial_invite;
}

