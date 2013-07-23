#ifndef SQLCALLPROFILE_H
#define SQLCALLPROFILE_H

#include "SBCCallProfile.h"

#include <string>

using std::string;

struct SqlCallProfile
        : public SBCCallProfile
{
    int time_limit;
    bool SQLexception;
    bool cached;
    struct timeval expire_time;
    list<string> dyn_fields;

    SqlCallProfile();
    ~SqlCallProfile();
};

#endif // SQLCALLPROFILE_H
