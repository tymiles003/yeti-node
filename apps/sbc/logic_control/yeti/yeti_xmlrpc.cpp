#include "yeti.h"

typedef void (Yeti::*YetiRpcHandler)(const AmArg& args, AmArg& ret);

struct xmlrpc_entry: public AmObject {
  YetiRpcHandler handler;
  string leaf_descr,func_descr,arg,arg_descr;
  AmArg leaves;

  xmlrpc_entry(string ld):
	  handler(NULL), leaf_descr(ld) {}

  xmlrpc_entry(string ld, YetiRpcHandler h, string fd):
	  leaf_descr(ld), handler(h), func_descr(fd) {}

  xmlrpc_entry(string ld, YetiRpcHandler h, string fd, string a, string ad):
	  leaf_descr(ld), handler(h), func_descr(fd), arg(a), arg_descr(ad) {}

  bool isMethod(){ return handler!=NULL; }
  bool hasLeafs(){ return leaves.getType()==AmArg::Struct; }
  bool hasLeaf(const char *leaf){ return hasLeafs()&&leaves.hasMember(leaf); }
};

void Yeti::init_xmlrpc_cmds(){
#define reg_leaf(parent,leaf,name,descr) \
	e = new xmlrpc_entry(descr);\
	parent[name] = e;\
	AmArg &leaf = e->leaves;

#define reg_method(parent,name,descr,func,func_descr) \
	e = new xmlrpc_entry(descr,&Yeti::func,func_descr);\
	parent[name] = e;

#define reg_leaf_method(parent,leaf,name,descr,func,func_descr) \
	reg_method(parent,name,descr,func,func_descr);\
	AmArg &leaf = e->leaves;

#define reg_method_arg(parent,name,descr,func,func_descr,arg, arg_descr) \
	e = new xmlrpc_entry(descr,&Yeti::func,func_descr,arg, arg_descr);\
	parent[name] = e;

#define reg_leaf_method_arg(parent,leaf,name,descr,func,func_descr,arg, arg_descr) \
	reg_method_arg(parent,name,descr,func,func_descr,arg, arg_descr);\
	AmArg &leaf = e->leaves;

	xmlrpc_entry *e;
	e = new xmlrpc_entry("root");
	xmlrpc_cmds = e->leaves;
	AmArg &root = xmlrpc_cmds;

	/* show */
	reg_leaf(root,show,"show","read only queries");

		reg_method(show,"version","show version",showVersion,"");

		reg_leaf(show,show_router,"router","active router instance");
			reg_method(show_router,"cache","show callprofile's cache state",ShowCache,"");

		reg_leaf(show,show_media,"media","media processor instance");
			reg_method(show_media,"streams","active media streams info",showMediaStreams,"");

		reg_leaf_method_arg(show,show_calls,"calls","active calls",GetCalls,"show current active calls",
						"<LOCAL-TAG>","retreive call by local_tag");
			reg_method(show_calls,"count","active calls count",GetCallsCount,"");

		reg_method(show,"configuration","actual settings",GetConfig,"");

		reg_method(show,"stats","runtime statistics",GetStats,"");

		reg_method(show,"interfaces","show network interfaces configuration",showInterfaces,"");

		reg_leaf_method(show,show_registrations,"registrations","uac registrations",GetRegistrations,"show configured uac registrations");
			reg_method(show_registrations,"count","active registrations count",GetRegistrationsCount,"");

	/* request */
	reg_leaf(root,request,"request","modify commands");
		reg_leaf(request,request_router,"router","active router instance");

			reg_method(request_router,"reload","reload active instance",reloadRouter,"");

			reg_leaf(request_router,request_router_cdrwriter,"cdrwriter","CDR writer instance");
				reg_method(request_router_cdrwriter,"close-files","immideatly close failover csv files",closeCdrFiles,"");

			reg_leaf(request_router,request_router_translations,"translations","disconnect/internal_db codes translator");
				reg_method(request_router_translations,"reload","reload translator",reloadTranslations,"");

			reg_leaf(request_router,request_router_codec_groups,"codec-groups","codecs groups configuration");
				reg_method(request_router_codec_groups,"reload","reload codecs-groups",reloadCodecsGroups,"");

			reg_leaf(request_router,request_router_resources,"resources","resources actions configuration");
				reg_method(request_router_resources,"reload","reload resources",reloadResources,"");

			reg_leaf(request_router,request_router_cache,"cache","callprofile's cache");
				reg_method(request_router_cache,"clear","clear cached profiles",ClearCache,"");

		reg_leaf(request,request_registrations,"registrations","uac registrations");
			reg_method(request_registrations,"reload","reload reqistrations preferences",reloadRegistrations,"");
			reg_method_arg(request_registrations,"renew","renew registration",RenewRegistration,
						   "","<ID>","renew registration by id");

		reg_leaf(request,request_stats,"stats","runtime statistics");
			reg_method(request_stats,"clear","clear all counters",ClearStats,"");

		reg_leaf(request,request_call,"call","active calls control");
			reg_method_arg(request_call,"disconnect","drop call",DropCall,
						   "","<LOCAL-TAG>","drop call by local_tag");

		reg_leaf(request,request_media,"media","media processor instance");
			reg_method_arg(request_media,"payloads","loaded codecs",showPayloads,"show supported codecs",
						   "cost","compute transcoding cost for each codec");

	/* set */
	//reg_leaf(root,set,"set","heavy queries");

#undef reg_leaf
#undef reg_method
#undef reg_leaf_method
#undef reg_method_arg
#undef reg_leaf_method_arg
}

void Yeti::process_xmlrpc_cmds(const AmArg cmds, const string& method, const AmArg& args, AmArg& ret){
	const char *list_method = "_list";
	//DBG("process_xmlrpc_cmds(%p,%s,...)",&cmds,method.c_str());
	if(method==list_method){
		switch(cmds.getType()){
			case AmArg::Struct: {
				//DBG("_list AmArg::Struct");
				AmArg::ValueStruct::const_iterator it = cmds.begin();
				for(;it!=cmds.end();++it){
					const AmArg &am_e = it->second;
					xmlrpc_entry *e = reinterpret_cast<xmlrpc_entry *>(am_e.asObject());
					AmArg f;
					f.push(it->first);
					f.push(e->leaf_descr);
					ret.push(f);
				}
			} break;

			case AmArg::AObject: {
				//DBG("_list AmArg::AObject");
				xmlrpc_entry *e = reinterpret_cast<xmlrpc_entry *>(cmds.asObject());
				if(!e->func_descr.empty()&&(!e->arg.empty()||e->hasLeafs())){
					AmArg f;
					f.push("[Enter]");
					f.push(e->func_descr);
					ret.push(f);
				}
				if(!e->arg.empty()){
					AmArg f;
					f.push(e->arg);
					f.push(e->arg_descr);
					ret.push(f);
				}
				if(e->hasLeafs()){
					const AmArg &l = e->leaves;
					AmArg::ValueStruct::const_iterator it = l.begin();
					for(;it!=l.end();++it){
						const AmArg &am_e = it->second;
						xmlrpc_entry *e = reinterpret_cast<xmlrpc_entry *>(am_e.asObject());
						AmArg f;
						f.push(it->first);
						f.push(e->leaf_descr);
						ret.push(f);
					}
				}
			} break;

			default:
				throw AmArg::TypeMismatchException();
		}
		return;
	}

	if(cmds.hasMember(method)){
		//DBG("hasMember(%s)",method.c_str());
		const AmArg &l = cmds[method];
		if(l.getType()!=AmArg::AObject)
			throw AmArg::TypeMismatchException();

		xmlrpc_entry *e = reinterpret_cast<xmlrpc_entry *>(l.asObject());
		//DBG("AmArg::AObject");
		if(args.size()>0){
			if(e->hasLeaf(args[0].asCStr())){
				AmArg nargs = args,sub_method;
				nargs.pop(sub_method);
				process_xmlrpc_cmds(e->leaves,sub_method.asCStr(),nargs,ret);
				return;
			} else if(args[0]==list_method){
				AmArg nargs = args,sub_method;
				nargs.pop(sub_method);
				process_xmlrpc_cmds(l,sub_method.asCStr(),nargs,ret);
				return;
			}
		}
		if(e->isMethod()){
			(this->*(e->handler))(args,ret);
			return;
		}
		throw AmDynInvoke::NotImplemented("missed arg");
	}
	throw AmDynInvoke::NotImplemented("no matches with methods tree");
}
