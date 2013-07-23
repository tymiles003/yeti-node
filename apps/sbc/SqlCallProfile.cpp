#include "SqlCallProfile.h"

SqlCallProfile::SqlCallProfile()
{
    DBG("SqlCallProfile(%p)",this);
    profile_file = "SQL";
}

SqlCallProfile::~SqlCallProfile()
{
    DBG("~SqlCallProfile(%p)",this);
}
