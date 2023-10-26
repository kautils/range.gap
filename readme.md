### kautil_range.gap
* the part of cache library

### example
```c++

#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>

template<typename premitive>
struct file_syscall_premitive{
    using value_type = premitive;
    using offset_type = long;

    int fd=-1;
    char * buffer = 0;
    offset_type buffer_size = 0;
    
    ~file_syscall_premitive(){ free(buffer); }
    offset_type block_size(){ return sizeof(value_type); }
    offset_type size(){ return lseek(fd,0,SEEK_END)- lseek(fd,0,SEEK_SET); }
    
    void read_value(offset_type const& offset, value_type ** value){
        lseek(fd,offset,SEEK_SET);
        ::read(fd,*value,sizeof(value_type));
    }
    
    bool write(offset_type const& offset, void ** data, offset_type size){
        lseek(fd,offset,SEEK_SET);
        return size==::write(fd,*data,size);
    }
    
    // api may make a confusion but correct. this is because to avoid copy of value(object such as bigint) for future.
    bool read(offset_type const& from, void ** data, offset_type size){
        lseek(fd,from,SEEK_SET);
        return size==::read(fd,*data,size);
    }
    
    bool extend(offset_type extend_size){ 
        return !ftruncate(fd,size()+extend_size);   
    }
    int shift(offset_type dst,offset_type src,offset_type size){
        if(buffer_size < size){
            if(buffer)free(buffer);
            buffer = (char*) malloc(buffer_size = size);
        }
        lseek(fd,src,SEEK_SET);
        auto read_size = ::read(fd,buffer,size);
        lseek(fd,dst,SEEK_SET);
        ::write(fd,buffer,read_size);
        return 0;
    }
    
    int flush_buffer(){ return 0; }
};

using file_syscall_16b_pref= file_syscall_premitive<uint64_t>;
using file_syscall_16b_f_pref= file_syscall_premitive<double>;


#include "gap.hpp"
int main(){
    
    using value_type = uint64_t;
    using offset_type = long;
    auto step = 10;
    auto data = std::vector<value_type>();{
        for(auto i = 0; i < 100; i+=2){
             data.push_back(i*step+step);
             data.push_back(data.back()+step);
        }
    }

    auto f_ranges = fopen("tmain_kautil_range_exsits_interface.cache","w+b");
    {
        auto written = fwrite(data.data(),sizeof(value_type),data.size(),f_ranges);
        fflush(f_ranges);

        auto fd = fileno(f_ranges);
        printf("file size : %ld\n",lseek(fd,0,SEEK_END)-lseek(fd,0,SEEK_SET));
    }

    auto pref = file_syscall_16b_pref{.fd=fileno(f_ranges)};
    auto gp = gap{&pref};
    gp.initialize(5,2000);
    {// entire
        for(auto const& elem : gp ){
            printf("l,r(%lld,%lld)\n",elem.l,elem.r);
            fflush(stdout);
        }
    }

    {// increment/decrement
        auto c = gp.begin();
        auto e = gp.end();
        auto lmb_print = [](auto elem){
            auto e = *elem;
            printf("%lld %lld\n",e.l,e.r); fflush(stdout);
        };
        lmb_print(c);
        lmb_print(++c);
        lmb_print(c+=2);
        lmb_print(c-=2);
        lmb_print(--c);
    }

    {// reinitialize
        gp.initialize(2000,2005);
        {// entire
            for(auto const& elem : gp ){
                printf("l,r(%lld,%lld)\n",elem.l,elem.r);
                fflush(stdout);
            }
        }
    }
    
    return 0;
}


```





