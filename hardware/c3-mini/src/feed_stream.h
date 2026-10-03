#pragma once
#include <cstddef>
#include <cstring>
// Extract each ac object while retaining a small JSON envelope with ac replaced
// by []. The caller validates both with its JSON decoder before committing data.
// Never retain a whole response. Reject malformed framing, bounds or truncation.
class AircraftStream {
    enum State {Start,KeyOrEnd,Key,Colon,Value,Other,ArrayFirst,ArrayNext,Record,AfterRecord,AfterValue,Done,Failed};
    State state=Start;
    bool quoted=false,escaped=false,found=false,allowEnd=true;
    int depth=0;
    size_t keySize=0,recordSize=0,envelopeSize=0;
    char key[64]{};
    bool append(char *buffer,size_t &length,size_t capacity,char c) {
        if(length+1>=capacity){fail("JSON field too large");return false;}
        buffer[length++]=c;buffer[length]=0;return true;
    }
    void meta(char c){append(envelope,envelopeSize,sizeof(envelope),c);}
    bool balance(char c){
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;return true;}
        if(c=='"')quoted=true;
        else if(c=='{'||c=='['){if(++depth>24){fail("JSON nesting too deep");return false;}}
        else if(c=='}'||c==']')--depth;
        return true;
    }
public:
    char record[6144]{},envelope[2048]{};
    const char *error=nullptr;
    size_t bytes=0,objects=0;
    void fail(const char *message){if(!error)error=message;state=Failed;}
    bool complete() const{return state==Done&&found&&!error;}
    template<class Consumer> bool put(char c,Consumer consume) {
        if(error)return false;
        if(++bytes>1024*1024){fail("Feed exceeds 1 MiB");return false;}
        bool ws=c==' '||c=='\t'||c=='\r'||c=='\n';
        switch(state){
        case Start: if(ws)break;if(c!='{'){fail("Expected JSON object");break;}meta(c);state=KeyOrEnd;break;
        case KeyOrEnd:
            if(ws)break;
            if(c=='}'&&allowEnd){meta(c);state=Done;break;}
            if(c!='"'){fail("Expected root key");break;}
            meta(c);keySize=0;escaped=false;state=Key;break;
        case Key:
            meta(c);
            if(c=='"'&&!escaped){key[keySize]=0;state=Colon;break;}
            append(key,keySize,sizeof(key),c);
            if(escaped)escaped=false;else if(c=='\\')escaped=true;
            break;
        case Colon: if(ws)break;if(c!=':'){fail("Expected colon");break;}meta(c);state=Value;break;
        case Value:
            if(ws)break;
            if(!std::strcmp(key,"ac")){
                if(found||c!='['){fail("Invalid aircraft array");break;}
                found=true;meta('[');meta(']');state=ArrayFirst;break;
            }
            depth=0;quoted=false;escaped=false;state=Other;
            if(c==','||c=='}'){fail("Missing root value");break;}
            meta(c);balance(c);break;
        case Other:
            if(!quoted&&depth==0&&(c==','||c=='}')){
                meta(c);allowEnd=false;state=c==','?KeyOrEnd:Done;break;
            }
            meta(c);balance(c);break;
        case ArrayFirst: case ArrayNext:
            if(ws)break;
            if(c==']'&&state==ArrayFirst){state=AfterValue;break;}
            if(c!='{'){fail("Expected aircraft object");break;}
            recordSize=0;quoted=false;escaped=false;depth=1;
            append(record,recordSize,sizeof(record),c);state=Record;break;
        case Record:
            if(!append(record,recordSize,sizeof(record),c))break;
            if(!balance(c))break;
            if(depth==0&&!quoted){
                if(!consume(record,recordSize)){fail("Aircraft JSON invalid");break;}
                ++objects;state=AfterRecord;
            }break;
        case AfterRecord:
            if(ws)break;
            if(c==',')state=ArrayNext;else if(c==']')state=AfterValue;else fail("Expected aircraft separator");break;
        case AfterValue:
            if(ws)break;
            if(c==','){meta(c);allowEnd=false;state=KeyOrEnd;}
            else if(c=='}'){meta(c);state=Done;}
            else fail("Expected root separator");break;
        case Done: if(!ws)fail("Trailing response data");break;
        case Failed:break;
        }
        return !error;
    }
};
