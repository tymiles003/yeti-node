#ifndef RESOURCE_H
#define RESOURCE_H

#include <vector>
#include <string>

using namespace std;

struct ResourceParseException {
  string what;
  string ctx;
  ResourceParseException(string w,string c) : what(w), ctx(c) {}
};

struct Resource {
	int id,					//unique id within type space
		type,				//determines behavior when resource is busy
		takes,				//how many takes one get()
		limit;				//upper limit for such active resources
};

typedef vector<Resource> ResourceList;

ResourceList resource_parse(const string s);
string resource_print(const Resource &r);

#endif // RESOURCE_H
