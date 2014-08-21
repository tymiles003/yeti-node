#include "TrustedHeaders.h"
#include "../yeti.h"

#include <pqxx/pqxx>

_TrustedHeaders::_TrustedHeaders() {}

_TrustedHeaders::~_TrustedHeaders() {}

int _TrustedHeaders::load_config(){
	int ret = 1;
	try {
		pqxx::result r;
		pqxx::connection c(dbc.conn_str());
		c.set_variable("search_path",db_schema+", public");

		pqxx::prepare::declaration d =
			//c.prepare("load_trusted_headers","SELECT name FROM unnest( ARRAY['SeRveR',$1] ) AS name");
			c.prepare("load_trusted_headers","SELECT * FROM load_trusted_headers($1)");

		//d("varchar",pqxx::prepare::treat_direct);
		d("integer",pqxx::prepare::treat_direct);

			pqxx::nontransaction t(c);

			pqxx::prepare::invocation invoc = t.prepared("load_trusted_headers");

			//invoc("Call-ID");
			invoc(Yeti::instance()->config.node_id);

			r = invoc.exec();
			for(pqxx::result::size_type i = 0; i < r.size();++i){
				const pqxx::result::tuple &row = r[i];

				string hdr = row["o_name"].c_str();
				if(hdr.empty()) continue;

				hdrs.push_back(hdr);
				DBG("TrustedHeader: %s",hdr.c_str());
			}
		t.commit();
		c.disconnect();
		ret = 0;
	} catch(const pqxx::pqxx_exception &e){
		ERROR("pqxx_exception: %s ",e.base().what());
	}
	return ret;
}

int _TrustedHeaders::count(){
	return hdrs.size();
}

int _TrustedHeaders::configure(AmConfigReader &cfg){
	db_schema = Yeti::instance()->config.routing_schema;
	configure_db(cfg);
	if(load_config()){
		ERROR("can't load trusted headers config");
		return -1;
	}

	return 0;
}

void _TrustedHeaders::configure_db(AmConfigReader &cfg){
	string prefix("master");
	dbc.cfg2dbcfg(cfg,prefix);
}

bool _TrustedHeaders::reload(){
	if(load_config()){
		return false;
	}
	return true;
}

void _TrustedHeaders::init_hdrs(vector<AmArg> &trusted_hdrs){
	trusted_hdrs.resize(hdrs.size(),AmArg());
}

void _TrustedHeaders::invocate(pqxx::prepare::declaration &d){
	for(vector<string>::const_iterator it =  hdrs.begin();
			it != hdrs.end(); ++it){
		d("varchar",pqxx::prepare::treat_direct);
	}
}

void _TrustedHeaders::parse_reply_hdrs(const AmSipReply &reply, vector<AmArg> &trusted_hdrs){
	int i = 0;
	DBG("TrustedHeaders::parse_reply_hdrs() reply.hdrs = '%s'",reply.hdrs.c_str());
	for(vector<string>::const_iterator it =  hdrs.begin();
			it != hdrs.end(); ++it, ++i)
	{
		string hdr = getHeader(reply.hdrs,*it);
		if(hdr.empty()){
			DBG("TrustedHeaders::parse_reply_hdrs() no header '%s' in reply",it->c_str());
			if(!isArgUndef(trusted_hdrs[i])) //don't overwrite non empty value
				trusted_hdrs[i] = AmArg();
		} else {
			DBG("TrustedHeaders::parse_reply_hdrs() got '%s' for header '%s'",
				hdr.c_str(),it->c_str());
			trusted_hdrs[i] = hdr;
		}
	}
}

void _TrustedHeaders::print_hdrs(const vector<AmArg> &trusted_hdrs){
	vector<string>::const_iterator hit = hdrs.begin();
	for(vector<AmArg>::const_iterator it = trusted_hdrs.begin();
		it != trusted_hdrs.end(); ++it, ++hit)
	{
		const AmArg &a = *it;
		const string &h = *hit;
		DBG("TrustedHeader: '%s' = %s",h.c_str(),a.print(a).c_str());
	}
}
