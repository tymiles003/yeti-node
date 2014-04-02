#ifndef _DBTYPES_
#define _DBTYPES_

#include <map>
#include <list>
#include <string>

#define GETPROFILE_STATIC_FIELDS_COUNT 18
#define WRITECDR_STATIC_FIELDS_COUNT 36

struct static_field {
    const char *name;
    const char *type;   //field SQL type
};

extern const static_field cdr_static_fields[];
extern const static_field profile_static_fields[];

using namespace std;

typedef vector<string> PreparedQueryArgs;
typedef PreparedQueryArgs::iterator PreparedQueryArgs_iterator;

typedef map<string, pair<string,PreparedQueryArgs> > PreparedQueriesT;
typedef PreparedQueriesT::iterator PreparedQueriesT_iterator;

typedef list< pair<string,string> > DynFieldsT;
typedef DynFieldsT::iterator DynFieldsT_iterator;

#endif
