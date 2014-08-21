#include "CallCtx.h"

inline void propagate_common_fields(list<SqlCallProfile *>::iterator &new_profile,const list<SqlCallProfile *>::iterator &old_profile){
	(*new_profile)->global_tag = (*old_profile)->global_tag;
}

CallCtx *getCtx(SBCCallLeg *call){
	return reinterpret_cast<CallCtx *>(call->getLogicData());
}

SqlCallProfile *CallCtx::getFirstProfile(){
	//DBG("%s() this = %p",FUNC_NAME,this);
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
	DBG("%s()",FUNC_NAME);

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

	propagate_common_fields(next_profile,current_profile);
	current_profile = next_profile;
	return *current_profile;
}

SqlCallProfile *CallCtx::getCurrentProfile(){
	if(current_profile == profiles.end())
		return NULL;
	return *current_profile;
}

int CallCtx::getOverrideId(bool aleg){
	if(current_profile == profiles.end())
		return 0;
	if(aleg){
		return (*current_profile)->aleg_override_id;
	}
	return (*current_profile)->bleg_override_id;
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
	SQLexception(false),
	cdr_processed(false),
	on_hold(false)
{
	router->inc();
	//DBG("%s() this = %p",FUNC_NAME,this);
}

CallCtx::~CallCtx(){
	//DBG("%s() this = %p",FUNC_NAME,this);
	list<SqlCallProfile *>::iterator it = profiles.begin();
	for(;it != profiles.end();++it){
		delete (*it);
	}
	if(initial_invite)
		delete initial_invite;
}

