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
	DBG("%s() this = %p",FUNC_NAME,this);
	++current_profile;
	if(current_profile == profiles.end()){
		return NULL;
	}
	if(!early_state){
		attempt_num++;
		cdr = new Cdr(**current_profile);
		cdr->attempt_num = attempt_num;
	}
	return *current_profile;
}

SqlCallProfile *CallCtx::getCurrentProfile(){
	if(current_profile == profiles.end())
		return NULL;
	return *current_profile;
}

ResourceList &CallCtx::getCurrentResourceList(){
	if(current_profile == profiles.end())
		throw AmSession::Exception(500, SIP_REPLY_SERVER_INTERNAL_ERROR);
	return (*current_profile)->rl;
}

CallCtx::CallCtx(){
	DBG("%s() this = %p",FUNC_NAME,this);
}

CallCtx::~CallCtx(){
	DBG("%s() this = %p",FUNC_NAME,this);
	list<SqlCallProfile *>::iterator it = profiles.begin();
	for(;it != profiles.end();++it){
		delete (*it);
	}
}

