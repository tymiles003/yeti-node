#ifndef _DBTYPES_
#define _DBTYPES_

#include <map>
#include <list>
#include <string>

#define GETPROFILE_STATIC_FIELDS_COUNT 18
#define WRITECDR_STATIC_FIELDS_COUNT 31

using namespace std;

typedef map<string, pair<string,int> > PreparedQueriesT;
typedef PreparedQueriesT::iterator PreparedQueriesT_iterator;

typedef list< pair<string,string> > DynFieldsT;
typedef DynFieldsT::iterator DynFieldsT_iterator;

#endif
