#include "CdrWriter.h"
#include "log.h"
#include "AmThread.h"
#include <pqxx/pqxx>
#include "Version.h"

static const char *static_fields_names[] = {
	"attempt_num",
	"is_last",
	"time_limit",
	"legA_local_ip",
	"legA_local_port",
	"legA_remote_ip",
	"legA_remote_port",
	"legB_local_ip",
	"legB_local_port",
	"legB_remote_ip",
	"legB_remote_port",
	"start_time",
	"connect_time",
	"end_time",
	"disconnect_code",
	"disconnect_reason",
	"disconnect_initiator",
	"disconnect_rewrited_code",
	"disconnect_rewrited_reason",
	"orig_call_id",
	"term_call_id",
	"local_tag",
	"msg_logger_path",
	"log_rtp",
	"log_sip"
};

CdrWriter::CdrWriter()
{

}

CdrWriter::~CdrWriter()
{

}

int CdrWriter::configure(CdrWriterCfg& cfg)
{
	config=cfg;
	DynFieldsT_iterator dit;
	/*show all fields*/
	int i;
	for(i = 0;i<WRITECDR_STATIC_FIELDS_COUNT;i++){
		DBG("%d: %s",i,static_fields_names[i]);
	}
	dit = config.dyn_fields.begin();
	for(;dit!=config.dyn_fields.end();++dit){
		i++;
		DBG("%d: %s",i,dit->first.c_str());
	}
	return 0;
}


void CdrWriter::start()
{
	cdrthreadpool_mut.lock();
	DBG("CdrWriter::start: Starting %d async DB threads",config.poolsize);
	for(unsigned int i=0;i<config.poolsize;i++){
		CdrThread* th = new CdrThread;
		th->configure(config);
		th->start();
		cdrthreadpool.push_back(th);
	}
	cdrthreadpool_mut.unlock();
}

void CdrWriter::stop()
{
	DBG("CdrWriter::stop: Begin shutdown cycle");
	cdrthreadpool_mut.lock();
	int len=cdrthreadpool.size();
	for(int i=0;i<len;i++){
		DBG("CdrWriter::stop: Try shutdown thread %d",i);
		CdrThread* th= cdrthreadpool.back();
		cdrthreadpool.pop_back();
		th->stop();
		delete th;
	}
	len=cdrthreadpool.size();
	DBG("CdrWriter::stop: LEN:: %d", len);
	cdrthreadpool_mut.unlock();
}

void CdrWriter::postcdr(Cdr* cdr )
{
	DBG("%s(%p)",FUNC_NAME,cdr);
	cdrthreadpool_mut.lock();
		cdrthreadpool[cdr->cdr_born_time.tv_usec%cdrthreadpool.size()]->postcdr(cdr);
	cdrthreadpool_mut.unlock();
}

void CdrWriter::getConfig(AmArg &arg){
	AmArg param;
	AmArg params;
	AmArg query;
	
	DynFieldsT_iterator dit = config.dyn_fields.begin();
	int param_num = 1;
	
	//static params
	for(;param_num<WRITECDR_STATIC_FIELDS_COUNT;param_num++){
		param.push(param_num);
		param.push(dit->second);
		params.push(param);
		param.clear();
	}
	//dynamic params
	for(;dit!=config.dyn_fields.end();++dit){
		param.push(param_num++);
		param.push(dit->first);
		param.push(dit->second);
		params.push(param);
		param.clear();
	}
		
	query["name"] = config.prepared_queries.begin()->first;
	query["query"] = config.prepared_queries.begin()->second.first;
	query["params"] = params;

	arg.push("queries",query);

}

void CdrWriter::getStats(AmArg &arg){
	AmArg underlying_stats,threads;

	arg["name"] = config.name;
	arg["poolsize"]= (int)config.poolsize;
	cdrthreadpool_mut.lock();
	for(vector<CdrThread*>::iterator it = cdrthreadpool.begin();it != cdrthreadpool.end();it++){
		(*it)->getStats(underlying_stats);
		threads.push(underlying_stats);
		underlying_stats.clear();
	}
	cdrthreadpool_mut.unlock();
	arg.push("threads",threads);
}

void CdrWriter::clearStats(){
	cdrthreadpool_mut.lock();
		for(vector<CdrThread*>::iterator it = cdrthreadpool.begin();it != cdrthreadpool.end();it++)
		(*it)->clearStats();
	cdrthreadpool_mut.unlock();
}

void CdrThread::postcdr(Cdr* cdr)
{
	DBG("%s[%p](%p)",FUNC_NAME,this,cdr);
	queue_mut.lock();
		//queue.push_back(newcdr);
		queue.push_back(cdr);
		queue_run.set(true);
	queue_mut.unlock();
}


CdrThread::CdrThread() : queue_run(false),stopped(false),masterconn(NULL),slaveconn(NULL),gotostop(false)
{
	clearStats();
}

CdrThread::~CdrThread()
{
	wf.close();
}

void CdrThread::getStats(AmArg &arg){
	queue_mut.lock();
		arg["queue_len"] = (int)queue.size();
	queue_mut.unlock();

	arg["stopped"] = stopped.get();
	arg["queue_run"] = queue_run.get();
	arg["db_exceptions"] = stats.db_exceptions;
	arg["writed_cdrs"] = stats.writed_cdrs;
	arg["tried_cdrs"] = stats.tried_cdrs;
}

void CdrThread::clearStats(){
	stats.db_exceptions = 0;
	stats.writed_cdrs = 0;
	stats.tried_cdrs = 0;
}

int CdrThread::configure(CdrThreadCfg& cfg ){
	config=cfg;
	queue_run.set(false);
	return 0;
}

void CdrThread::on_stop(){
	INFO("Stopping CdrWriter thread");
	gotostop=true;
	queue_run.set(true); // we must switch thread to run state for exit.
	stopped.wait_for();

	int pid=masterconn->backendpid();
	DBG("CdrWriter: Disconnect master SQL. Backend pid: %d.",pid);
	masterconn->disconnect();
	if(slaveconn){
		pid=slaveconn->backendpid();
		DBG("CdrWriter: Disconnect slave SQL. Backend pid: %d.",pid);
		slaveconn->disconnect();
	}
}

void CdrThread::run(){
	INFO("Starting CdrWriter thread");
	connectdb();
while(true){
	Cdr* cdr;
	queue_run.wait_for();
	if (gotostop){
		stopped.set(true);
		return;
	}

	DBG("CdrWriter cycle startup");

	queue_mut.lock();
		cdr=queue.front();
		queue.pop_front();
		if(0==queue.size()){
			DBG("CdrWriter cycle stop.Empty queue");
			queue_run.set(false);
		}
	queue_mut.unlock();

	bool cdr_writed = false;
	if(0!=writecdr(masterconn,cdr)){
		ERROR("Cant write CDR to master database");
		if (config.failover_to_slave) {
			DBG("failover_to_slave enabled. try");
			if(!slaveconn || 0!=writecdr(slaveconn,cdr)){
				ERROR("Cant write CDR to slave database");
				if(config.failover_to_file){
					DBG("failover_to_file enabled. try");
					if(0!=writecdrtofile(cdr)){
						ERROR("can't write CDR to file");
					} else {
						//succ writed to file
						cdr_writed = true;
					}
				} else {
					DBG("failover_to_file disabled");
				}
			} else {
				//succ writed to slave database
				cdr_writed = true;
				wf.close(); //close failover file
			}
		} else {
			DBG("failover_to_slave disabled");
			if(config.failover_to_file){
				DBG("failover_to_file enabled. try");
				if(0!=writecdrtofile(cdr)){
					ERROR("can't write CDR to file");
				} else {
					//succ writed to file
					cdr_writed = true;
				}
			} else {
				DBG("failover_to_file disabled");
			}
		}
	} else {
		//succ writed to master database
		cdr_writed = true;
		wf.close(); //close failover file
	}

	if(cdr_writed){
		stats.writed_cdrs++;
		DBG("CDR deleted from queue");
		delete cdr;
	} else {
		DBG("return CDR into queue");
		queue_mut.lock();
			queue.push_back(cdr);
			queue_run.set(true);
		queue_mut.unlock();
	}
} //while
}

void CdrThread::prepare_queries(pqxx::connection *c){
	PreparedQueriesT_iterator it = config.prepared_queries.begin();
	DynFieldsT_iterator dit;

	for(;it!=config.prepared_queries.end();++it){
		pqxx::prepare::declaration d = c->prepare(it->first,it->second.first);
		//static fields
		for(int i = 0;i<WRITECDR_STATIC_FIELDS_COUNT;i++){
			d("varchar",pqxx::prepare::treat_direct);
		}
		//dynamic fields
		dit = config.dyn_fields.begin();
		for(;dit!=config.dyn_fields.end();++dit){
			d(dit->second,pqxx::prepare::treat_direct);
		}
		c->prepare_now(it->first);
	}
}

int CdrThread::_connectdb(pqxx::connection **conn,string conn_str){
	pqxx::connection *c = NULL;
	int ret = 0;
	try{
		c = new pqxx::connection(conn_str);
		if (c->is_open()){
			prepare_queries(c);
			DBG("CdrWriter: SQL connected. Backend pid: %d.",c->backendpid());
		}
		*conn = c;
		ret = 1;
	} catch(const pqxx::broken_connection &e){
		DBG("CdrWriter: SQL connection exception: %s",e.what());
	} catch(const pqxx::undefined_function &e){
		ERROR("CdrWriter: SQL connection: undefined_function query: %s, what: %s",e.query().c_str(),e.what());
		c->disconnect();
		throw std::runtime_error("CdrThread exception");
	}
	return ret;
}

int CdrThread::connectdb(){
	int ret = _connectdb(&masterconn,config.masterdb.conn_str());
	if(config.failover_to_slave){
		ret|=_connectdb(&slaveconn,config.slavedb.conn_str());
	}
	return ret;
}

int CdrThread::writecdr(pqxx::connection* conn, Cdr* cdr){
	DBG("%s[%p](conn = %p,cdr = %p)",FUNC_NAME,this,conn,cdr);
	int ret = 1;
	pqxx::result r;
	pqxx::nontransaction tnx(*conn);
	stats.tried_cdrs++;
	try{

		if(!tnx.prepared("writecdr").exists()){
			ERROR("have no prepared SQL statement");
			return 1;
		}

		pqxx::prepare::invocation invoc = tnx.prepared("writecdr");
		/* invocate static fields */
		invoc(cdr->attempt_num);
		invoc(cdr->is_last);
		invoc(cdr->time_limit);
		invoc(cdr->legA_local_ip);
		invoc(cdr->legA_local_port);
		invoc(cdr->legA_remote_ip);
		invoc(cdr->legA_remote_port);
		invoc(cdr->legB_local_ip);
		invoc(cdr->legB_local_port);
		invoc(cdr->legB_remote_ip);
		invoc(cdr->legB_remote_port);
		invoc(cdr->start_time.tv_sec);
		invoc(cdr->connect_time.tv_sec);
		invoc(cdr->end_time.tv_sec);
		invoc(cdr->disconnect_code);
		invoc(cdr->disconnect_reason);
		invoc(cdr->disconnect_initiator);
		if(cdr->is_last){
			invoc(cdr->disconnect_rewrited_code);
			invoc(cdr->disconnect_rewrited_reason);
		} else {
			invoc(0);
			invoc("");
		}
		invoc(cdr->orig_call_id);
		invoc(cdr->term_call_id);
		invoc(cdr->local_tag);
		invoc(cdr->msg_logger_path);
		invoc(cdr->log_rtp);
		invoc(cdr->log_sip);
		/* invocate dynamic fields  */
		list<string>::iterator it = cdr->dyn_fields.begin();
		for(;it!=cdr->dyn_fields.end();++it){
			invoc((*it));
		}
		r = invoc.exec();
		if (r.size()!=0&&0==r[0][0].as<int>()){
			ret = 0;
		}
	} catch(const pqxx::pqxx_exception &e){
		DBG("SQL exception on CdrWriter thread: %s",e.base().what());
		stats.db_exceptions++;
	}
	return ret;
}

bool CdrThread::openfile(){
	if(!wf.is_open()){
		ostringstream path;
		path << config.failover_file_dir << "/" <<
				std::dec << time(NULL) << "-" << this << ".csv";
		wf.open(path.str().c_str(), std::ofstream::out | std::ofstream::trunc);
		if(!wf.is_open())
			return false;
		write_header();
		return true;
	}
	return true;
}

void CdrThread::write_header(){
		//write description header
	wf << "#version: " << YETI_VERSION << endl;
	wf << "#static_fields_count: " << WRITECDR_STATIC_FIELDS_COUNT << endl;
	wf << "#dynamic_fields_count: " << config.dyn_fields.size() << endl;
		//static fields names
	wf << "#fields_descr: ";
	for(int i = 0;i<WRITECDR_STATIC_FIELDS_COUNT;i++){
		if(i) wf << ",";
		wf << "'" << static_fields_names[i] << "'";
	}
		//dynamic fields names
	DynFieldsT_iterator dit = config.dyn_fields.begin();
	for(;dit!=config.dyn_fields.end();++dit){
		wf << ",'"<< dit->first << "'";
	}
	wf << endl;
	wf.flush();
}

int CdrThread::writecdrtofile(Cdr* cdr){
	#define wv(v) "'"<<v<< "'" << ','
	if(!openfile()){
		return -1;
	}
	wf << std::dec <<
		//static fields
	wv(cdr->attempt_num) << wv(cdr->is_last) << wv(cdr->time_limit) <<
	wv(cdr->legA_local_ip) << wv(cdr->legA_local_port) <<
	wv(cdr->legA_remote_ip) << wv(cdr->legA_remote_port) <<
	wv(cdr->legB_local_ip) << wv(cdr->legB_local_port) <<
	wv(cdr->legB_remote_ip) << wv(cdr->legB_remote_port) <<
	wv(cdr->start_time.tv_sec) << wv(cdr->connect_time.tv_sec) <<
	wv(cdr->end_time.tv_sec) << wv(cdr->disconnect_code) <<
	wv(cdr->disconnect_reason) << wv(cdr->disconnect_initiator);
	if(cdr->is_last){
		wf << 	wv(cdr->disconnect_rewrited_code) <<
		wv(cdr->disconnect_rewrited_reason);
	} else {
		wf << wv(0) << wv("");
	}
	wf << wv(cdr->orig_call_id) << wv(cdr->term_call_id) <<
	wv(cdr->local_tag) << wv(cdr->msg_logger_path) <<
	wv(cdr->log_rtp) << wv(cdr->log_sip);
		//dynamic fields
	list<string>::iterator	it = cdr->dyn_fields.begin(),
							lit = cdr->dyn_fields.end();
	lit--;
	for(;it!=lit;++it)
		wf << wv(*it);
	wf << "'" << *lit << endl;
	wf.flush();
	stats.writed_cdrs++;
	return 0;
}

int CdrThreadCfg::cfg2CdrThCfg(AmConfigReader& cfg, string& prefix){
	string suffix="master"+prefix;
	string cdr_file_dir = prefix+"_dir";

	if(!cfg.hasParameter(cdr_file_dir)){
		ERROR("missed '%s'' parameter",cdr_file_dir.c_str());
		return -1;
	}

	failover_to_file = cfg.getParameterInt("failover_to_file",1);
	failover_file_dir = cfg.getParameter(cdr_file_dir);

	masterdb.cfg2dbcfg(cfg,suffix);
	suffix="slave"+prefix;
	slavedb.cfg2dbcfg(cfg,suffix);

	return 0;
}

int CdrWriterCfg::cfg2CdrWrCfg(AmConfigReader& cfg){
	string var=name+"_pool_size";
	if (cfg.hasParameter(var)){
		poolsize=cfg.getParameterInt(var);
	} else {
		poolsize=10;
		WARN("Variable %s not found in config. Using: %d",var.c_str(),poolsize);
	}
	return cfg2CdrThCfg(cfg,name);
}
