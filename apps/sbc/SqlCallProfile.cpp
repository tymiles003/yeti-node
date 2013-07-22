#include "SqlCallProfile.h"

SqlCallProfile::SqlCallProfile()
{
    DBG("SqlCallProfile(%p)",this);
    profile_file = "SQL";
}

SqlCallProfile::~SqlCallProfile()
{
    DBG("~SqlCallProfile(%p)",this);
    DBG("~SqlCallProfile(%p) transcoder.audio_codecs_str = %s",this,transcoder.audio_codecs_str.c_str());
    DBG("~SqlCallProfile(%p) transcoder.audio_codecs_norelay_aleg_str = %s",this,transcoder.audio_codecs_norelay_aleg_str.c_str());
}
