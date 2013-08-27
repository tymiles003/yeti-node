#include "CodesTranslator.h"
#include "DbConfig.h"
#include <pqxx/pqxx>

CodesTranslator* CodesTranslator::_instance=0;

CodesTranslator::CodesTranslator(){

}

CodesTranslator::~CodesTranslator(){
}

CodesTranslator* CodesTranslator::instance()
{
	if(!_instance)
		_instance = new CodesTranslator();
	return _instance;
}

int CodesTranslator::configure(AmConfigReader &cfg){
	int ret = 0;
	string prefix("master");
	DbConfig dbc;
	pqxx::result r;
	dbc.cfg2dbcfg(cfg,prefix);
	try {
		pqxx::connection c(dbc.conn_str());
		pqxx::work t(c);
			r = t.exec("SELECT * from switch.load_disconnect_code_rerouting()");

			for(pqxx::result::size_type i = 0; i < r.size();++i){
				const pqxx::result::tuple &row = r[i];
				int code = row["received_code"].as<int>(0);
				pref p(row["stop_rerouting"].as<bool>(true));

				code2pref.insert(pair<int,pref>(code,p));
				DBG("ResponsePref:     %d -> stop_hunting: %d",
					code,p.is_stop_hunting);
			}

			r = t.exec("SELECT * from switch.load_disconnect_code_rewrite()");
			for(pqxx::result::size_type i = 0; i < r.size();++i){
				const pqxx::result::tuple &row = r[i];
				int code =	row["o_code"].as<int>(0);
				trans t(	row["o_pass_reason_to_originator"].as<bool>(false),
							row["o_rewrited_code"].as<int>(code),
							row["o_rewrited_reason"].c_str());

				code2trans.insert(pair<int,trans>(code,t));
				DBG("ResponseTrans:     %d -> %d:'%s' pass_reason: %d",
					code,t.rewrite_code,t.rewrite_reason.c_str(),t.pass_reason_to_originator);
			}
		t.commit();
		c.disconnect();
	} catch(const pqxx::pqxx_exception &e){
		ERROR("pqxx_exception: %s ",e.base().what());
		ret = 1;
	}
	return ret;
}

void CodesTranslator::rewrite_response(unsigned int code,const string &reason,
				  unsigned int &out_code,string &out_reason){
	map<int,trans>::const_iterator it = code2trans.find(code);
	if(it!=code2trans.end()){
		const trans &t = it->second;
		string treason = reason;
		out_code = t.rewrite_code;
		out_reason = t.pass_reason_to_originator?treason:t.rewrite_reason;
		DBG("translated %d:'%s' -> %d:'%s'",
			code,treason.c_str(),
			out_code,out_reason.c_str());
	} else {
		DBG("no translation for response with code '%d'. leave it 'as is'",code);
		out_code = code;
		out_reason = reason;
	}
}

bool CodesTranslator::stop_hunting(unsigned int code){
	map<int,pref>::const_iterator it = code2pref.find(code);
	if(it!=code2pref.end()){
		bool stop = it->second.is_stop_hunting;
		DBG("stop_hunting = %d for code '%d'",stop,code);
		return stop;
	} else {
		DBG("no preference for code '%d', so simply stop hunting",code);
		return true;
	}
}

