#pragma once
// Small strict JSON reader for the public manifest. No external dependency.
#include <cmath>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace depthtrack::tea::json {
struct Value {
    enum Kind { Null,Boolean,Number,String,Array,Object } kind=Null;
    double number=0;
    std::string string;
    std::vector<Value> array;
    std::map<std::string,Value> object;
    const Value& at(const std::string& key) const {
        if(kind!=Object)throw std::runtime_error("Manifest: expected object");
        auto i=object.find(key);if(i==object.end())throw std::runtime_error("Manifest: missing "+key);return i->second;
    }
    const Value* find(const std::string& key) const {
        if(kind!=Object)return nullptr;
        auto i=object.find(key);return i==object.end()?nullptr:&i->second;
    }
    std::string text() const { if(kind!=String)throw std::runtime_error("Manifest: expected string");return string; }
    double numeric() const { if(kind!=Number)throw std::runtime_error("Manifest: expected number");return number; }
};
class Reader {
    const std::string& s;std::size_t i=0;
    [[noreturn]] void bad() const {throw std::runtime_error("Invalid JSON at byte "+std::to_string(i));}
    void ws(){while(i<s.size()&&(s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t'))++i;}
    bool take(char c){ws();if(i<s.size()&&s[i]==c){++i;return true;}return false;}
    unsigned hex4(){unsigned v=0;for(int n=0;n<4;++n){if(i==s.size())bad();char c=s[i++];unsigned x=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;if(x>15)bad();v=v*16+x;}return v;}
    void utf8(std::string& out,unsigned cp){if(cp<=127)out+=char(cp);else if(cp<=2047){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}else if(cp<=65535){out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}else{out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}}
    std::string str(){if(!take('"'))bad();std::string out;while(i<s.size()){unsigned char c=s[i++];if(c=='"')return out;if(c<32)bad();if(c!='\\'){out+=char(c);continue;}if(i==s.size())bad();switch(s[i++]){case '"':out+='"';break;case '\\':out+='\\';break;case '/':out+='/';break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;case 'u':{unsigned cp=hex4();if(cp>=0xd800&&cp<=0xdbff){if(i+2>s.size()||s[i++]!='\\'||s[i++]!='u')bad();unsigned lo=hex4();if(lo<0xdc00||lo>0xdfff)bad();cp=0x10000+((cp-0xd800)<<10)+lo-0xdc00;}else if(cp>=0xdc00&&cp<=0xdfff)bad();utf8(out,cp);break;}default:bad();}}bad();}
    Value value(unsigned depth){if(depth>64)bad();ws();if(i==s.size())bad();Value v;
        if(s[i]=='{'){++i;v.kind=Value::Object;if(take('}'))return v;do{auto key=str();if(!take(':'))bad();if(!v.object.emplace(key,value(depth+1)).second)bad();}while(take(','));if(!take('}'))bad();return v;}
        if(s[i]=='['){++i;v.kind=Value::Array;if(take(']'))return v;do{v.array.push_back(value(depth+1));}while(take(','));if(!take(']'))bad();return v;}
        if(s[i]=='"'){v.kind=Value::String;v.string=str();return v;}
        for(auto token:{std::string("null"),std::string("true"),std::string("false")})if(s.compare(i,token.size(),token)==0){i+=token.size();v.kind=token=="null"?Value::Null:Value::Boolean;v.number=token=="true";return v;}
        std::size_t begin=i;if(s[i]=='-')++i;if(i==s.size())bad();if(s[i]=='0')++i;else{if(s[i]<'1'||s[i]>'9')bad();while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;}if(i<s.size()&&s[i]=='.'){++i;auto start=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;if(i==start)bad();}if(i<s.size()&&(s[i]=='e'||s[i]=='E')){++i;if(i<s.size()&&(s[i]=='+'||s[i]=='-'))++i;auto start=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;if(i==start)bad();}v.kind=Value::Number;v.number=std::strtod(s.substr(begin,i-begin).c_str(),nullptr);if(!std::isfinite(v.number))bad();return v;
    }
public:explicit Reader(const std::string& input):s(input){}Value read(){Value v=value(0);ws();if(i!=s.size())bad();return v;}
};
} // namespace depthtrack::tea::json
