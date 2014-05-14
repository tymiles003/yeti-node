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

void get_codec_cost(int payload_id,const unsigned char *buf, int size, AmArg &cost){
#define DEFAULT_SDP_PARAMS ""
	AmPlugIn* plugin = AmPlugIn::instance();
	long h_codec;
	//int frame_size;
	int i=0;
	double encode_cost = -1, decode_cost = -1;
	amci_codec_t *codec = NULL;

	amci_payload_t *payload = plugin->payload(payload_id);
	if(!payload) goto fin;

	DBG("payload = %p, codec_id = %d",payload,payload->codec_id);
	/*amci_codec_t **/codec = plugin->codec(payload->codec_id);
	if(!codec) goto fin;

	DBG("codec = %p, codec.init = %p",codec,codec->init);
	if(!codec->init) goto fin;

	amci_codec_fmt_info_t fmt_i[4];
	fmt_i[0].id = 0;
	h_codec = (*codec->init)(DEFAULT_SDP_PARAMS, fmt_i);
	if(-1==h_codec){
		ERROR("can't init codec %d",payload_id);
		goto fin;
	}

	while (fmt_i[i].id) {
		switch (fmt_i[i].id) {
		case AMCI_FMT_FRAME_LENGTH : {
			DBG("AMCI_FMT_FRAME_LENGTH %d",fmt_i[i].value);
		} break;
		case AMCI_FMT_FRAME_SIZE: {
			DBG("AMCI_FMT_FRAME_SIZE %d",fmt_i[i].value);
			//frame_size=fmt_i[i].value;
		} break;
		case AMCI_FMT_ENCODED_FRAME_SIZE: {
			DBG("AMCI_FMT_ENCODED_FRAME_SIZE %d",fmt_i[i].value);
		} break;
		default: {
		  DBG("Unknown codec format descriptor: %d\n", fmt_i[i].id);
		} break;
		}
		i++;
	}

	/* encode cost */
	if(codec->encode){
		//
	}

	/* decode cost */
	if(codec->decode){
	}

	if(codec->destroy)
		(*codec->destroy)(h_codec);
fin:
	cost["encode"] = encode_cost;
	cost["decode"] = decode_cost;
}
