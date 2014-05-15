#include "codecs_bench.h"

#include "AmPlugIn.h"
#include "AmAudioFile.h"

int load_testing_source(string path,unsigned char *&buf){
	AmAudioFile f;

	int desired_length = 1; //seconds

	if(f.open(path,AmAudioFile::Read)){
		ERROR("can't open file");
		return -1;
	}

	if(f.getLength()<(desired_length*1e3)){
		ERROR("file should contain at least %ds but has length: %dms",
			  desired_length,f.getLength());
		return -1;
	}

	unsigned int samples = desired_length*8e3; //one second length
	unsigned int buf_size = samples << 1;

	buf = new unsigned char[buf_size];
	unsigned char *p = buf;
	for(unsigned int i =0; i< samples;i++)
		p+=f.get(0,p,8000,1);

	return p-buf;
}

void get_codec_cost(int payload_id,unsigned char *buf, int size, AmArg &cost){
#define DEFAULT_SDP_PARAMS ""

	long h_codec = -1;
	int out_buf_size,ret;
	amci_codec_fmt_info_t fmt_i[4];
	amci_codec_t *codec = NULL;
	unsigned char *out_buf,*tmp_buf;
	timeval start,end,diff;

	/** init codec */

	AmPlugIn* plugin = AmPlugIn::instance();
	amci_payload_t *payload = plugin->payload(payload_id);
	if(!payload) return;

	//DBG("payload = %p[%s], codec_id = %d",payload,payload->name,payload->codec_id);
	codec = plugin->codec(payload->codec_id);
	if(!codec) return;

	//DBG("codec = %p, codec.init = %p",codec,codec->init);

	cost["pcm16_size"] = size;

	if(codec->init){
		fmt_i[0].id = 0;
		h_codec = (*codec->init)(DEFAULT_SDP_PARAMS, fmt_i);
		int i=0;
		while (fmt_i[i].id) {
			switch (fmt_i[i].id) {
			case AMCI_FMT_FRAME_LENGTH : {
				//DBG("AMCI_FMT_FRAME_LENGTH %d",fmt_i[i].value);
				cost["frame_length"] = fmt_i[i].value;
			} break;
			case AMCI_FMT_FRAME_SIZE: {
				//DBG("AMCI_FMT_FRAME_SIZE %d",fmt_i[i].value);
				//frame_size=fmt_i[i].value;
				cost["frame_size"] = fmt_i[i].value;
			} break;
			case AMCI_FMT_ENCODED_FRAME_SIZE: {
				//DBG("AMCI_FMT_ENCODED_FRAME_SIZE %d",fmt_i[i].value);
				cost["encoded_frame_size"] = fmt_i[i].value;
			} break;
			default: {
			  DBG("Unknown codec format descriptor: %d\n", fmt_i[i].id);
			} break;
			}
			i++;
		}
	}

	/** encode cost */

	if(!codec->encode||!codec->decode){
		DBG("codec for payload %s. doesn't have either encode or decode func",payload->name);
		return;
	}

	//DBG("codec->samples2bytes = %p",codec->samples2bytes);

	out_buf_size = codec->samples2bytes ? (*codec->samples2bytes)(h_codec,8e3) : size;
	out_buf = new unsigned char[out_buf_size];
	//DBG("out_buf[%p,%d]",out_buf,out_buf_size);

	gettimeofday(&start,NULL);
		ret = (*codec->encode)(out_buf,buf,size,1,8e3,h_codec);
	gettimeofday(&end,NULL);

	if(codec->destroy)
		(*codec->destroy)(h_codec);
	if(ret<0){
		ERROR("%s.encode = %d",payload->name,ret);
		delete[] out_buf;
		return;
	}

	timersub(&end,&start,&diff);
	double encode_cost = diff.tv_sec+diff.tv_usec/1e6;
	double encode_chps = 1/encode_cost;

	cost["encoded_size"] = ret;
	cost["encode_cost"] = encode_cost;
	cost["encode_chps"] = encode_chps;

	/** decode cost */
	tmp_buf = new unsigned char[size];
	if(codec->init){
		fmt_i[0].id = 0;
		h_codec = (*codec->init)(DEFAULT_SDP_PARAMS, fmt_i);
	}

	gettimeofday(&start,NULL);
		ret = (*codec->decode)(tmp_buf,out_buf,ret,1,8e3,h_codec);
	gettimeofday(&end,NULL);

	if(codec->destroy)
		(*codec->destroy)(h_codec);
	if(ret<0){
		ERROR("%s.decode = %d",payload->name,ret);
		delete[] tmp_buf;
		return;
	}
	timersub(&end,&start,&diff);
	double  decode_cost = diff.tv_sec+diff.tv_usec/1e6;
	double  decode_chps = 1/decode_cost;

	cost["decode_cost"] = decode_cost;
	cost["decode_chps"] = decode_chps;

	/** both cost */
	double both_cost = decode_cost+encode_cost;
	double both_chps = 1/both_cost;

	cost["both_cost"] = both_cost;
	cost["both_chps"] = both_chps;

	delete[] out_buf;
}
