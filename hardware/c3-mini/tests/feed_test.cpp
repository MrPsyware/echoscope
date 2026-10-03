#include "../src/decoder.h"
#include <cassert>
#include <string>
#include <fstream>
#include <iterator>
#include <iostream>
bool decode(mini::Decoder &d,const std::string &s){for(char c:s)if(!d.put(c))return false;return d.finish();}
int main(int argc,char **argv){
    const std::string a=R"({"hex":"abc123","flight":"TEST1  ","lat":51.5,"lon":0.01,"seen_pos":1,"alt_baro":12000,"gs":180,"track":90,"nav_modes":["autopilot"],"r":"G-TEST","t":"A320"})";
    mini::Decoder good(51.5,0,2);assert(decode(good,"{\"now\":123,\"ac\":["+a+"],\"msg\":\"No error\"}"));assert(good.snapshot.count==1);assert(!strcmp(good.snapshot.aircraft[0].call,"TEST1"));assert(good.snapshot.aircraft[0].alt==12000);
    mini::Decoder surface(51.5,0,2);
    assert(decode(surface,R"({"ac":[{"hex":"ground1","lat":51.5,"lon":0.01,"seen_pos":0,"alt_baro":"ground","alt_geom":100,"gs":30},{"hex":"air1","lat":51.5,"lon":0.01,"seen_pos":0,"alt_baro":0,"gs":0},{"hex":"air2","lat":51.5,"lon":0.01,"seen_pos":0,"alt_baro":-50},{"hex":"air3","lat":51.5,"lon":0.01,"seen_pos":0,"alt_baro":200}]})"));
    assert(surface.groundSkipped==1&&surface.snapshot.count==3&&surface.snapshot.total==3);
    assert(!strcmp(surface.snapshot.aircraft[0].hex,"air1")&&surface.snapshot.aircraft[0].alt==0);
    mini::Decoder empty(0,0,2);assert(decode(empty,"{\"ac\":[],\"msg\":\"No error\"}"));assert(!empty.snapshot.count);
    const char *bad[]={"{}","{\"ac\":null}","{\"ac\":[}","{\"ac\":[{},]}","{\"ac\":[],}","{\"ac\":[],\"ac\":[]}","{\"ac\":[]} trailing","{\"ac\":[],\"msg\":\"error\"}","{\"ac\":[{\"nav_modes\":[\"a,\"seen\":1}]}"};
    for(auto s:bad){mini::Decoder d(0,0,2);assert(!decode(d,s));}
    std::string feed="{\"ac\":["+a+"],\"other\":{\"string\":\"brace } and escaped \\\" [\",\"array\":[1,2,3]}}";
    mini::Decoder nested(51.5,0,2);assert(decode(nested,feed));
    for(size_t n=0;n<feed.size();++n){mini::Decoder d(51.5,0,2);assert(!decode(d,feed.substr(0,n)));}
    mini::Decoder bounds(51.5,0,2);assert(!decode(bounds,"{\"ac\":[{\"huge\":\""+std::string(7000,'x')+"\"}]}"));
    std::string crowded="{\"ac\":[";for(int i=0;i<200;++i){if(i)crowded+=",";crowded+=a;}crowded+="]}";
    mini::Decoder many(51.5,0,2);assert(decode(many,crowded));assert(many.snapshot.count==32&&many.snapshot.total==200);
    mini::Snapshot snap;for(int n=100;n>0;--n){mini::Aircraft p;p.km=float(n);mini::keep(snap,p);}assert(snap.count==32&&snap.total==100&&snap.aircraft[0].km==1&&snap.aircraft[31].km==32);
    float east,north;assert(mini::project(0,-179.99,0,179.99,east,north)&&east>0&&east<3);
    if(argc>1){std::ifstream f(argv[1]);std::string raw((std::istreambuf_iterator<char>(f)),{});mini::Decoder d(51.5,0,2);bool ok=decode(d,raw);std::cout<<"live fixture: "<<ok<<" bytes="<<d.stream.bytes<<" kept="<<d.snapshot.count<<" candidates="<<d.snapshot.total<<" error="<<d.error<<'\n';assert(ok);}
}
