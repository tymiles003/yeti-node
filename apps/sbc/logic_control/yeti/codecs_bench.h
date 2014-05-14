#ifndef CODECS_BENCH_H
#define CODECS_BENCH_H

#include "AmArg.h"

#define DEFAULT_BECH_FILE_PATH "/usr/lib/sems/audio/GE_ISR 1_1.wav"

int load_testing_source(string path,unsigned char *&buf);
void get_codec_cost(int payload_id,const unsigned char *buf, int size, AmArg &cost);

#endif // CODECS_BENCH_H
