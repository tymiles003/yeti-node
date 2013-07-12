#include "SqlCallProfile.h"

SqlCallProfile::SqlCallProfile()
{
    DBG("SqlCallProfile(%p)",this);
    profile_file = "SQL";
}

SqlCallProfile::SqlCallProfile(const SqlCallProfile &profile)
{
    DBG("SqlCallProfile(%p,%p)",this,&profile);
}

SqlCallProfile::~SqlCallProfile()
{
    DBG("~SqlCallProfile(%p)",this);
}
