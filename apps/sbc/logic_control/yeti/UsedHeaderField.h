#ifndef USEDHEADERFIELD_H
#define USEDHEADERFIELD_H

#include "AmSipMsg.h"

#include <pqxx/result>
#include <string>

using std::string;

class UsedHeaderField {

    enum NeededPart {
        uri_user,
        uri_domain,
        uri_port
    };

    enum ValueType {
        Raw,
        Uri
    };

    string name;        //SIP hdr name
    ValueType type;     //type of header value
    NeededPart part;    //needed part of parsed value
    bool hashkey;       //this header used in routing logic

  public:
    UsedHeaderField(const string &hdr_name);
    UsedHeaderField(const pqxx::result::tuple &t);

    void readFromTuple(const pqxx::result::tuple &t);
    bool getValue(const AmSipRequest &req,string &val) const;
    void getInfo(AmArg &arg) const;

    const string &getName() const { return name; }
    bool is_hashkey() const { return hashkey; }
};

#endif // USEDHEADERFIELD_H