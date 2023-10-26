#ifdef TMAIN_KAUTIL_RANGE_GAP_INTERFACE

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
    
    using value_type = uint64_t;/**/
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

    auto is_contained = [](auto & b0){
        auto block_size =(sizeof(value_type)*2);
        auto b0_nearest_is_former = !bool(b0.nearest_pos % block_size);
        auto b0_cond_not_contained = 
                 (b0_nearest_is_former&(b0.direction < 0))
                |(!b0_nearest_is_former&(b0.direction > 0));
        return !(b0_cond_not_contained|b0.overflow);
    };
    
    auto pref = file_syscall_16b_pref{.fd=fileno(f_ranges)};
    auto max_pos = pref.size()-sizeof(value_type);
    auto min_pos = 0;
    
    constexpr auto kBlockSize = offset_type(sizeof(value_type)*2);
    auto from = value_type(0);auto to = value_type(0);
    {
        { // !c(from) !c(to) : expect {(25,30),(40,45)} idx(1 2) vp (1 2) b,e(1 3)
            from = 25;to = 45;  
        }

        {// !c(from)  c(to) : expect {(25,30),(40,50)} idx(1 2) vp (1 npos) b,e(1 3)
            from = 25;to = 55; 
            from = 25;to = 50;
            from = 25;to = 60;
        }

        {// c(from)  c(to) : expect {(20,30),(40,50)} idx(1 2) vp (npos npos) b,e(1 3)
            from = 10;to = 55;  
            from = 15;to = 55; 
            from = 20;to = 55; 
        }

        {// ovf(from)  c(to) : expect {(5,first),(20,30),(40,50)} idx(0 2) vp (0 npos) b,e(0 3)
            from = 5;to = 55; 
            from = 5;to = 50; 
            from = 5;to = 60; 
        }
        {// ovf(from)  !c(to) : expect {(5,first),(20,30),(40,45)} idx(0 2) vp (0 2) b,e(0 3)
            from = 5;to = 45; 
        }
        {// c(from)  ovf(to) : expect {(20,30)...(last,2000)} idx(1 fsize/blockSize) vp (-3 fsize/blockSize) b,e(1 fsize/blockSize+1)
            from = 15;to = 2000; 
        }

        {// !c(from)  ovf(to) : expect {(25,30)...(last,2000)} idx(1 fsize/blockSize) vp (1 fsize/blockSize) b,e(1 fsize/blockSize+1)
            from = 25;to = 2000; 
        }


        {// ovfovf(different) : expect {(5,first)...(last,2000)} idx(0 fsize/blockSize) vp (0 fsize/blockSize) b,e(0 fsize/blockSize+1)
            from = 5;to = 2000; 
        }

        {// ovfovf(same(left-side)) : expect {(0,5)}  idx(-1 -1) vp (-1 -1) b,e(0 0)
            from = 0;to = 5; 
        }

        {// ovfovf(same(right-side)) : expect {(2000,2005)} idx(-1 -1) vp (-1 -1) b,e(0 0)
            from = 2000;to = 2005; 
        }
        
        from = 5;to = 2000; 
        
        printf("from,to(%lld,%lld)\n",from,to);fflush(stdout);
    }
        
    auto gp = gap{&pref};
    gp.initialize(from,to);
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
        lmb_print(++c);
        lmb_print(--c);
        lmb_print(--c);
        lmb_print(c+=2);
        lmb_print(c-=2);
        lmb_print(++c);
        lmb_print(c--);
        lmb_print(c++);
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

#endif
