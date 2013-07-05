
#include "AmPlugIn.h"
#include "log.h"
#include "AmArg.h"

#include "lm_localfs.h"
#include "AmSession.h"
#include <string.h>

class LMLocalFSFactory : public AmDynInvokeFactory
{
public:
    LMLocalFSFactory(const string& name)
	: AmDynInvokeFactory(name) {}

    AmDynInvoke* getInstance(){
    return LMLocalFS::instance();
    }

    int onLoad(){
      if (LMLocalFS::instance()->onLoad())
	return -1;

      DBG("template logic control loaded.\n");

      return 0;
    }
};

EXPORT_PLUGIN_CLASS_FACTORY(LMLocalFSFactory, MOD_NAME);

LMLocalFS* LMLocalFS::_instance=0;

LMLocalFS* LMLocalFS::instance()
{
    if(!_instance)
    _instance = new LMLocalFS();
    return _instance;
}

LMLocalFS::LMLocalFS()
{
}

LMLocalFS::~LMLocalFS() { }

int LMLocalFS::onLoad() {
  if(cfg.loadFile(AmConfig::ModConfigPath + string(MOD_NAME ".conf"))) {
    ERROR("No configuration for logic module present (%s)\n",
     (AmConfig::ModConfigPath + string(MOD_NAME ".conf")).c_str()
     );
    return -1;
  }

  vector<string> profiles_names = explode(cfg.getParameter("profiles"), ",");
  for (vector<string>::iterator it =
     profiles_names.begin(); it != profiles_names.end(); it++) {
    string profile_file_name = AmConfig::ModConfigPath + *it + ".sbcprofile.conf";
    if (!call_profiles[*it].readFromConfiguration(*it, profile_file_name)) {
      ERROR("configuring SBC call profile from '%s'\n", profile_file_name.c_str());
      return -1;
    }
  }

  active_profile = explode(cfg.getParameter("active_profile"), ",");
  if (active_profile.empty()) {
    ERROR("active_profile not set.\n");
    return -1;
  }

  string active_profile_s;
  for (vector<string>::iterator it =
     active_profile.begin(); it != active_profile.end(); it++) {
    if (it->empty())
      continue;
    if (((*it)[0] != '$') && call_profiles.find(*it) == call_profiles.end()) {
      ERROR("call profile active_profile '%s' not loaded!\n", it->c_str());
      return -1;
    }
    active_profile_s+=*it;
    if (it != active_profile.end()-1)
      active_profile_s+=", ";
  }

  INFO("SBC: active profile: '%s'\n", active_profile_s.c_str());

  return 0;
}

void LMLocalFS::invoke(const string& method, const AmArg& args, AmArg& ret)
{
  DBG("LMLocalFS: %s(%s)\n", method.c_str(), AmArg::print(args).c_str());
  if (method == "listProfiles"){
    listProfiles(args, ret);
  } else if (method == "reloadProfiles"){
    reloadProfiles(args,ret);
  } else if (method == "loadProfile"){
    args.assertArrayFmt("u");
    loadProfile(args,ret);
  } else if (method == "reloadProfile"){
    args.assertArrayFmt("u");
    reloadProfile(args,ret);
  } else if (method == "getActiveProfile"){
    getActiveProfile(args,ret);
  } else if (method == "setActiveProfile"){
    args.assertArrayFmt("u");
    setActiveProfile(args,ret);
  } else if (method == "getLogicInterfaceHandler"){
    SBCLogicInterface *iface = this;
    ret[0] = (AmObject *)iface;
  } else if(method == "_list"){
    ret.push(AmArg("listProfiles"));
    ret.push(AmArg("reloadProfiles"));
    ret.push(AmArg("reloadProfile"));
    ret.push(AmArg("loadProfile"));
    ret.push(AmArg("getActiveProfile"));
    ret.push(AmArg("setActiveProfile"));
    ret.push(AmArg("getLogicInterfaceHandler"));
  }
  else
    throw AmDynInvoke::NotImplemented(method);
}



SBCCallProfile& LMLocalFS::getCallProfile( const AmSipRequest& req,
                                        ParamReplacerCtx& ctx,
                                        getProfileRequestType RequestType )
{
    profiles_mut.lock();

    SBCCallProfile* p_call_profile = getActiveProfileMatch(req, ctx);
    if(!p_call_profile) {
      profiles_mut.unlock();
      throw AmSession::Exception(500,SIP_REPLY_SERVER_INTERNAL_ERROR);
    }

    //copy
    SBCCallProfile *p_call_profile_copy = new SBCCallProfile(*p_call_profile);

    profiles_mut.unlock();

    SBCCallProfile &call_profile(*p_call_profile_copy);

    return call_profile;
}

SBCCallProfile* LMLocalFS::getActiveProfileMatch(const AmSipRequest& req,
                          ParamReplacerCtx& ctx)
{
  string profile, profile_rule;
  vector<string>::const_iterator it = active_profile.begin();
  for (; it != active_profile.end(); it++) {

    if (it->empty())
      continue;

    if (*it == "$(paramhdr)")
      profile = get_header_keyvalue(ctx.app_param,"profile");
    else if (*it == "$(ruri.user)")
      profile = req.user;
    else
      profile = ctx.replaceParameters(*it, "active_profile", req);

    if (!profile.empty()) {
      profile_rule = *it;
      break;
    }
  }

  DBG("active profile = %s\n", profile.c_str());

  map<string, SBCCallProfile>::iterator prof_it = call_profiles.find(profile);
  if (prof_it==call_profiles.end()) {
    ERROR("could not find call profile '%s'"
      " (matching active_profile rule: '%s')\n",
      profile.c_str(), profile_rule.c_str());

    return NULL;
  }

  DBG("using call profile '%s' (from matching active_profile rule '%s')\n",
      profile.c_str(), profile_rule.c_str());

  return &prof_it->second;
}

void LMLocalFS::listProfiles(const AmArg& args, AmArg& ret) {
  profiles_mut.lock();
  for (std::map<string, SBCCallProfile>::iterator it=
     call_profiles.begin(); it != call_profiles.end(); it++) {
    AmArg p;
    p["name"] = it->first;
    p["md5"] = it->second.md5hash;
    p["path"] = it->second.profile_file;
    ret.push((p));
  }
  profiles_mut.unlock();
}

void LMLocalFS::reloadProfiles(const AmArg& args, AmArg& ret) {
  std::map<string, SBCCallProfile> new_call_profiles;

  bool failed = false;
  string res = "OK";
  AmArg profile_list;
  profiles_mut.lock();
  for (std::map<string, SBCCallProfile>::iterator it=
     call_profiles.begin(); it != call_profiles.end(); it++) {
    new_call_profiles[it->first] = SBCCallProfile();
    if (!new_call_profiles[it->first].readFromConfiguration(it->first,
                                it->second.profile_file)) {
      ERROR("reading call profile file '%s'\n", it->second.profile_file.c_str());
      res = "Error reading call profile for "+it->first+" from "+it->second.profile_file+
    +"; no profiles reloaded";
      failed = true;
      break;
    }
    AmArg p;
    p["name"] = it->first;
    p["md5"] = it->second.md5hash;
    p["path"] = it->second.profile_file;
    profile_list.push(p);
  }
  if (!failed) {
    call_profiles = new_call_profiles;
    ret.push(200);
  } else {
    ret.push(500);
  }
  ret.push(res);
  ret.push(profile_list);
  profiles_mut.unlock();
}

void LMLocalFS::reloadProfile(const AmArg& args, AmArg& ret) {
  bool failed = false;
  string res = "OK";
  AmArg p;
  if (!args[0].hasMember("name")) {
    ret.push(400);
    ret.push("Parameters error: expected ['name': profile_name] ");
    return;
  }

  profiles_mut.lock();
  std::map<string, SBCCallProfile>::iterator it=
    call_profiles.find(args[0]["name"].asCStr());
  if (it == call_profiles.end()) {
    res = "profile '"+string(args[0]["name"].asCStr())+"' not found";
    failed = true;
  } else {
    SBCCallProfile new_cp;
    if (!new_cp.readFromConfiguration(it->first, it->second.profile_file)) {
      ERROR("reading call profile file '%s'\n", it->second.profile_file.c_str());
      res = "Error reading call profile for "+it->first+" from "+it->second.profile_file;
      failed = true;
    } else {
      it->second = new_cp;
      p["name"] = it->first;
      p["md5"] = it->second.md5hash;
      p["path"] = it->second.profile_file;
    }
  }
  profiles_mut.unlock();

  if (!failed) {
    ret.push(200);
    ret.push(res);
    ret.push(p);
  } else {
    ret.push(500);
    ret.push(res);
  }
}

void LMLocalFS::loadProfile(const AmArg& args, AmArg& ret) {
  if (!args[0].hasMember("name") || !args[0].hasMember("path")) {
    ret.push(400);
    ret.push("Parameters error: expected ['name': profile_name] "
         "and ['path': profile_path]");
    return;
  }
  SBCCallProfile cp;
  if (!cp.readFromConfiguration(args[0]["name"].asCStr(), args[0]["path"].asCStr())) {
    ret.push(500);
    ret.push("Error reading sbc call profile for "+string(args[0]["name"].asCStr())+
         " from file "+string(args[0]["path"].asCStr()));
    return;
  }

  profiles_mut.lock();
  call_profiles[args[0]["name"].asCStr()] = cp;
  profiles_mut.unlock();
  ret.push(200);
  ret.push("OK");
  AmArg p;
  p["name"] = args[0]["name"];
  p["md5"] = cp.md5hash;
  p["path"] = args[0]["path"];
  ret.push(p);
}

void LMLocalFS::getActiveProfile(const AmArg& args, AmArg& ret) {
  profiles_mut.lock();
  AmArg p;
  for (vector<string>::iterator it=active_profile.begin();
       it != active_profile.end(); it++) {
    p["active_profile"].push(*it);
  }
  profiles_mut.unlock();
  ret.push(200);
  ret.push("OK");
  ret.push(p);
}

void LMLocalFS::setActiveProfile(const AmArg& args, AmArg& ret) {
  if (!args[0].hasMember("active_profile")) {
    ret.push(400);
    ret.push("Parameters error: expected ['active_profile': <active_profile list>] ");
    return;
  }
  profiles_mut.lock();
  active_profile = explode(args[0]["active_profile"].asCStr(), ",");
  profiles_mut.unlock();
  ret.push(200);
  ret.push("OK");
  AmArg p;
  p["active_profile"] = args[0]["active_profile"];
  ret.push(p);
}
