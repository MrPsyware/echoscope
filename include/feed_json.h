#pragma once
#include <ArduinoJson.h>
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace sky {
// ArduinoJson reads this by reference, so the consumed position survives errors.
// A byte reader also preserves embedded NULs in the captured response for diagnosis.
struct FeedJsonReader {
    const char *data;
    size_t size;
    size_t position=0;
    int read() { return position<size ? static_cast<unsigned char>(data[position++]) : -1; }
    size_t readBytes(char *buffer,size_t count) {
        count=std::min(count,size-position);
        std::memcpy(buffer,data+position,count); position+=count; return count;
    }
};
inline DeserializationError decodeFeed(JsonDocument &doc,FeedJsonReader &reader) {
    JsonDocument filter;
    const char *fields[]={"hex","flight","r","t","lat","lon","alt_baro","alt_geom","gs","track","baro_rate","seen_pos","desc","category","dbFlags"};
    for(auto field:fields) filter["ac"][0][field]=true;
    return deserializeJson(doc,reader,DeserializationOption::Filter(filter));
}
}
