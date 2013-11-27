#include "UsedHeaderField.h"

#include "AmUtils.h"
#include "sip/parse_nameaddr.h"
#include "sip/parse_uri.h"

using std::string;

UsedHeaderField::UsedHeaderField(const string &hdr_name){
    name = hdr_name;
    type = Raw;
}

UsedHeaderField::UsedHeaderField(const pqxx::result::tuple &t){
    readFromTuple(t);
}

void UsedHeaderField::readFromTuple(const pqxx::result::tuple &t){
    string format;

    name = t["varname"].c_str();
    hashkey = t["varhashkey"].as<bool>(false);
    format = t["varformat"].c_str();

    if(format.empty()){
        type = Raw;
        return;
    }

    if(format=="uri_user"){
        type = Uri;
        part = uri_user;
    } else if(format=="uri_domain"){
        type = Uri;
        part = uri_domain;
    } else if(format=="uri_port"){
        type = Uri;
        part = uri_port;
    } else {
        WARN("unknown format '%s' for header field '%s'. use Raw",
            format.c_str(),name.c_str());
        type = Raw;
    }
}

bool UsedHeaderField::getValue(const AmSipRequest &req,string &val) const {
    string hdr;
    const char *sptr;
    sip_nameaddr na;
    sip_uri uri;

    hdr = getHeader(req.hdrs,name);
    if(hdr.empty()){
        DBG("UsedHeaderField::getValue('%s') no such header in SipRequest",name.c_str());
        return false;
    }
    switch(type){

        case Raw:
            val = hdr;
            goto succ;
        break;

        case Uri:
            sptr = hdr.c_str();
            if(parse_nameaddr(&na,&sptr,hdr.length()) < 0 ||
               parse_uri(&uri,na.addr.s,na.addr.len) < 0)
            {
                ERROR("UsedHeaderField::getValue('%s') invalid uri '%s'",
                    name.c_str(),hdr.c_str());
                return false;
            }
            switch(part){
                case uri_user:
                    val = c2stlstr(uri.user);
                    goto succ;
                case uri_domain:
                    val = c2stlstr(uri.host);
                    goto succ;
                case uri_port:
                    val = int2str(uri.port);
                    goto succ;
                default:
                ERROR("UsedHeaderField::getValue('%s') unknown part type",
                        name.c_str());
                    return false;
            }
        break;

        default:
        ERROR("UsedHeaderField::getValue('%s') unknown value type",
                  name.c_str());
            return false;
    }
    return false;
succ:
    DBG("UsedHeaderField::getValue('%s') processed. return '%s'",
        name.c_str(),val.c_str());
    return true;
}

void UsedHeaderField::getInfo(AmArg &arg) const{
    string s;
    arg["name"] = name;
    arg["hashkey"] = hashkey;
    switch(type){
        case Raw: s = "Raw"; break;
        case Uri: s = "Uri"; break;
        default: s = "Unknown"; break;
    }
    arg["type"] = s;
    if(type!=Raw){
        switch(part){
            case uri_user: s = "uri_user"; break;
            case uri_domain: s = "uri_domain"; break;
            case uri_port: s = "uri_port"; break;
            default: s = "unknown"; break;
        }
        arg["part"] = s;
    }
}

