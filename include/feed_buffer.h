#pragma once
#include <cstdlib>
#include <cstring>
#include <memory>

namespace sky {
// Fixed capacity avoids Arduino 3.1.1 String growth truncating lengths to uint16_t.
// Appends copy exactly the received bytes, with no read past the source chunk.
class FeedBuffer {
    std::unique_ptr<char,decltype(&std::free)> data_{nullptr,&std::free};
    size_t capacity_=0, size_=0;
public:
    explicit FeedBuffer(size_t capacity,void *(*allocate)(size_t)=std::malloc)
        : data_(static_cast<char*>(allocate(capacity+1)),&std::free),capacity_(capacity) {
        if(data_) data_.get()[0]=0;
    }
    bool allocated() const { return bool(data_); }
    size_t length() const { return size_; }
    const char *c_str() const { return data_?data_.get():""; }
    char operator[](size_t at) const { return data_.get()[at]; }
    bool append(const char *bytes,size_t count) {
        if(!data_ || count>capacity_-size_) return false;
        std::memcpy(data_.get()+size_,bytes,count);
        size_+=count; data_.get()[size_]=0; return true;
    }
};
}
