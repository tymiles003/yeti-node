#include "CdrWriter.h"
#include "log.h"
#include "AmThread.h"
#include <pqxx/pqxx>
#include "Version.h"
#include "yeti.h"

static const char *static_fields_names[] = {
	"node_id",
	"pop_id",
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
	"log_sip",
	"legA_rx_payloads",
	"legA_tx_payloads",
	"legB_rx_payloads",
	"legB_tx_payloads"
};

static string join_str_vector(const vector<string> v,const string delim){
	std::stringstream ss;
	for(vector<string>::const_iterator i = v.begin();i!=v.end();++i){
		if(i != v.begin())
			ss << delim;
		ss << *i;
	}
	return string(ss.str());
}

CdrWriter::CdrWriter()
{

}

CdrWriter::~CdrWriter()
{

}

int CdrWriter::configure(CdrWriterCfg& cfg)
{
	config=cfg;

	//show all query args
	int param_num = 1;
	//static params
	for(;param_num<=WRITECDR_STATIC_FIELDS_COUNT;param_num++){
		DBG("CdrWriterArg:     %d: %s : varchar [static]",param_num,static_fields_names[param_num-1]);
	}
	//dynamic params
	DynFieldsT_iterator dit = config.dyn_fields.begin();
	for(;dit!=config.dyn_fields.end();++dit){
		DBG("CdrWriterArg:     %d: %s : %s [dynamic]",param_num++,dit->first.c_str(),dit->second.c_str());
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
	//DBG("%s(%p)",FUNC_NAME,cdr);
	cdrthreadpool_mut.lock();
		cdrthreadpool[cdr->cdr_born_time.tv_usec%cdrthreadpool.size()]->postcdr(cdr);
	cdrthreadpool_mut.unlock();
}

void CdrWriter::getConfig(AmArg &arg){
	AmArg params;

	arg["failover_to_slave"] = config.failover_to_slave;

	int param_num = 1;
	//static params
	for(;param_num<=WRITECDR_STATIC_FIELDS_COUNT;param_num++){
		params.push(int2str(param_num)+": "+string(static_fields_names[param_num-1])+" : "+"varchar");
	}
	//dynamic params
	DynFieldsT_iterator dit = config.dyn_fields.begin();
	for(;dit!=config.dyn_fields.end();++dit){
		params.push(int2str(param_num++)+": "+dit->first+" : "+dit->second);
	}
	arg.push("query_args",params);

	arg["failover_to_file"] = config.failover_to_file;
	if(config.failover_to_file){
		arg["failover_file_dir"] = config.failover_file_dir;
		arg["failover_file_completed_dir"] = config.failover_file_completed_dir;
	}

	arg["master_db"] = config.masterdb.conn_str();
	if(config.failover_to_slave){
		arg["slave_db"] = config.slavedb.conn_str();
	}
}

void CdrWriter::closeFiles(){
	for(vector<CdrThread*>::iterator it = cdrthreadpool.begin();it != cdrthreadpool.end();it++){
		(*it)->closefile();
	}
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
	//DBG("%s[%p](%p)",FUNC_NAME,this,cdr);
	queue_mut.lock();
		//queue.push_back(newcdr);
		queue.push_back(cdr);
		queue_run.set(true);
	queue_mut.unlock();
}


CdrThread::CdrThread() :
	queue_run(false),stopped(false),
	masterconn(NULL),slaveconn(NULL),gotostop(false),
	masteralarm(false),slavealarm(false)
{
	clearStats();
}

CdrThread::~CdrThread()
{
	closefile();
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
	setThreadName("YetiCdrWriter");
	if(!connectdb()){
		ERROR("can't connect to any DB on startup. give up");
		throw std::logic_error("CdrWriter can't connect to any DB on startup");
	}
while(true){
	Cdr* cdr;

	bool qrun = queue_run.wait_for_to(config.check_interval);

	if (gotostop){
		stopped.set(true);
		return;
	}

	if(!qrun){
//		DBG("queue condition wait timeout. check connections");
		//check master conn
		if(masterconn!=NULL){
			try {
				pqxx::work t(*masterconn);
				t.commit();
			} catch (const pqxx::pqxx_exception &e) {
				delete masterconn;
				if(!_connectdb(&masterconn,config.masterdb.conn_str())){
					if(!masteralarm){
						ERROR("CdrWriter %p master DB connection failed alarm raised",this);
						masteralarm = true;
					}
				} else {
					INFO("CdrWriter %p master DB connection failed alarm cleared",this);
					masteralarm = false;
				}
			}
		} else {
			if(!_connectdb(&masterconn,config.masterdb.conn_str())){
				if(!masteralarm){
					ERROR("CdrWriter %p master DB connection failed alarm raised",this);
					masteralarm = true;
				}
			} else {
				INFO("CdrWriter %p master DB connection failed alarm cleared",this);
				masteralarm = false;
			}
		}
		//check slave connecion
		if(!config.failover_to_slave)
			continue;

		if(slaveconn!=NULL){
			try {
				pqxx::work t(*slaveconn);
				t.commit();
			} catch (const pqxx::pqxx_exception &e) {
				delete slaveconn;
				if(!_connectdb(&slaveconn,config.slavedb.conn_str())){
					if(!slavealarm){
						ERROR("CdrWriter %p slave DB connection failed alarm raised",this);
						slavealarm = true;
					}
				} else {
					INFO("CdrWriter %p slave DB connection failed alarm cleared",this);
					slavealarm = false;
				}
			}
		} else {
			if(!_connectdb(&slaveconn,config.slavedb.conn_str())){
				if(!slavealarm){
					ERROR("CdrWriter %p slave DB connection failed alarm raised",this);
					slavealarm = true;
				}
			} else {
				INFO("CdrWriter %p slave DB connection failed alarm cleared",this);
				slavealarm = false;
			}
		}
		continue;
	}

	//DBG("CdrWriter cycle beginstartup");

	queue_mut.lock();
		cdr=queue.front();
		queue.pop_front();
		if(0==queue.size()){
			//DBG("CdrWriter cycle stop.Empty queue");
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
						DBG("CDR was written into file");
						cdr_writed = true;
					}
				} else {
					DBG("failover_to_file disabled");
				}
			} else {
				//succ writed to slave database
				DBG("CDR was written into slave");
				cdr_writed = true;
				closefile();
			}
		} else {
			DBG("failover_to_slave disabled");
			if(config.failover_to_file){
				DBG("failover_to_file enabled. try");
				if(0!=writecdrtofile(cdr)){
					ERROR("can't write CDR to file");
				} else {
					//succ writed to file
					DBG("CDR was written into file");
					cdr_writed = true;
				}
			} else {
				DBG("failover_to_file disabled");
			}
		}
	} else {
		//succ writed to master database
		DBG("CDR was written into master");
		cdr_writed = true;
		closefile();
	}

	if(cdr_writed){
		stats.writed_cdrs++;
		DBG("CDR deleted from queue");
		delete cdr;
	} else {
		/*
		DBG("return CDR into queue");
		queue_mut.lock();
			queue.push_back(cdr);
			queue_run.set(true);
		queue_mut.unlock();
		*/
		//FIXME: what we should really do here ?
		ERROR("CDR write failed. forget about it");
		delete cdr;
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
			INFO("CdrWriter: SQL connected. Backend pid: %d.",c->backendpid());
			ret = 1;
		}
	} catch(const pqxx::broken_connection &e){
			DBG("CdrWriter: SQL connection exception: %s",e.what());
		delete c;
	} catch(const pqxx::undefined_function &e){
			DBG("CdrWriter: SQL connection: undefined_function query: %s, what: %s",e.query().c_str(),e.what());
		c->disconnect();
		delete c;
		throw std::runtime_error("CdrThread exception");
	} catch(...){
	}
	*conn = c;
	return ret;
}

int CdrThread::connectdb(){
	int ret;
	ret = _connectdb(&masterconn,config.masterdb.conn_str());
	if(config.failover_to_slave){
		ret|=_connectdb(&slaveconn,config.slavedb.conn_str());
	}
	return ret;
}

int CdrThread::writecdr(pqxx::connection* conn, Cdr* cdr){
	DBG("%s[%p](conn = %p,cdr = %p)",FUNC_NAME,this,conn,cdr);
	int ret = 1;
	Yeti::global_config &gc = Yeti::instance()->config;

	if(conn==NULL){
		ERROR("writecdr() we got NULL connection pointer.");
		return 1;
	}

	stats.tried_cdrs++;
	try{
		pqxx::result r;
		pqxx::nontransaction tnx(*conn);

		if(!tnx.prepared("writecdr").exists()){
			ERROR("have no prepared SQL statement");
			return 1;
		}

		pqxx::prepare::invocation invoc = tnx.prepared("writecdr");
		/* invocate static fields */
		invoc(gc.node_id);
		invoc(gc.pop_id);
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

		invoc(join_str_vector(cdr->legA_incoming_payloads,","));
		invoc(join_str_vector(cdr->legA_outgoing_payloads,","));
		invoc(join_str_vector(cdr->legB_incoming_payloads,","));
		invoc(join_str_vector(cdr->legB_outgoing_payloads,","));

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
		conn->disconnect();
		stats.db_exceptions++;
	}
	return ret;
}

bool CdrThread::openfile(){
	if(wfp.get()&&wfp->is_open()){
		return true;
	} else {
		wfp.reset(new ofstream());
		ostringstream filename;
		char buf[80];
		time_t nowtime;
		struct tm timeinfo;

		time(&nowtime);
		localtime_r (&nowtime,&timeinfo);
		strftime (buf,80,"%G%m%d_%H%M%S",&timeinfo);
		filename << "/" << std::dec << buf << "_" << std::dec << this << ".csv";
		write_path = config.failover_file_dir+filename.str();
		completed_path = config.failover_file_completed_dir+filename.str();
		wfp->open(write_path.c_str(), std::ofstream::out | std::ofstream::trunc);
		if(!wfp->is_open()){
			ERROR("can't open '%s'. skip writing",write_path.c_str());
			return false;
		}
		DBG("write cdr file header");
		write_header();
		return true;
	}
	return false;
}

void CdrThread::closefile(){
	if(!wfp.get())
		return;
	wfp->flush();
	wfp->close();
	wfp.reset();
	if(0==rename(write_path.c_str(),completed_path.c_str())){
		ERROR("moved from '%s' to '%s'",write_path.c_str(),completed_path.c_str());
	} else {
		ERROR("can't move file from '%s' to '%s'",write_path.c_str(),completed_path.c_str());
	}
}

void CdrThread::write_header(){
	ofstream &wf = *wfp.get();
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
	ofstream &wf = *wfp.get();

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
	wf << "'" << *lit << "'" << endl;
	wf.flush();
	stats.writed_cdrs++;
	return 0;
}

int CdrThreadCfg::cfg2CdrThCfg(AmConfigReader& cfg, string& prefix){
	string suffix="master"+prefix;
	string cdr_file_dir = prefix+"_dir";
	string cdr_file_completed_dir = prefix+"_completed_dir";

	failover_to_file = cfg.getParameterInt("failover_to_file",1);
	if(failover_to_file){
		if(!cfg.hasParameter(cdr_file_dir)){
			ERROR("missed '%s'' parameter",cdr_file_dir.c_str());
			return -1;
		}
		if(!cfg.hasParameter(cdr_file_completed_dir)){
			ERROR("missed '%s'' parameter",cdr_file_completed_dir.c_str());
			return -1;
		}
		failover_file_dir = cfg.getParameter(cdr_file_dir);
		failover_file_completed_dir = cfg.getParameter(cdr_file_completed_dir);

		//check for permissions
		ofstream t1;
		ostringstream dir_test_file;
		dir_test_file << failover_file_dir << "/test";
		t1.open(dir_test_file.str().c_str(),std::ofstream::out | std::ofstream::trunc);
		if(!t1.is_open()){
			ERROR("can't write test file in '%s' directory",failover_file_dir.c_str());
			return -1;
		}
		remove(dir_test_file.str().c_str());

		ofstream t2;
		ostringstream completed_dir_test_file;
		completed_dir_test_file << failover_file_completed_dir << "/test";
		t2.open(completed_dir_test_file.str().c_str(),std::ofstream::out | std::ofstream::trunc);
		if(!t2.is_open()){
			ERROR("can't write test file in '%s' directory",failover_file_completed_dir.c_str());
			return -1;
		}
		remove(completed_dir_test_file.str().c_str());
	}

	masterdb.cfg2dbcfg(cfg,suffix);
	suffix="slave"+prefix;
	slavedb.cfg2dbcfg(cfg,suffix);

	return 0;
}

int CdrWriterCfg::cfg2CdrWrCfg(AmConfigReader& cfg){
	poolsize=cfg.getParameterInt(name+"_pool_size",10);
	check_interval = cfg.getParameterInt("cdr_check_interval",5000);
	failover_to_slave = cfg.getParameterInt("cdr_failover_to_slave",1);
	return cfg2CdrThCfg(cfg,name);
}
