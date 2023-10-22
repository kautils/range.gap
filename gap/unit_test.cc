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



#include "kautil/algorithm/btree_search/btree_search.hpp"

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
    auto written = fwrite(data.data(),sizeof(value_type),data.size(),f_ranges);
    fflush(f_ranges);


    {
        auto fd = fileno(f_ranges);
        printf("file size : %ld\n",lseek(fd,0,SEEK_END)-lseek(fd,0,SEEK_SET));
        
        auto pref = file_syscall_16b_pref{.fd=fd};
        auto bt = kautil::algorithm::btree_search{&pref};
        
        
        auto from = value_type(0);
        auto to = value_type(0);
        
        auto b0 = bt.search(from,false);
        auto b1 = bt.search(to,false);
        
        
        auto adjust_pos = [](auto & b0, auto const& b0_adjust_pos){
            auto block_size =(sizeof(value_type)*2);
            auto b0_is_former = !bool(b0.nearest_pos % block_size);
            auto b0_cond_not_contained = !b0_is_former&(b0.direction < 0);
            auto b0_cond_over_flow = b0.overflow;
            auto b0_cond_adjust_pos = !(b0_cond_not_contained|b0_cond_over_flow);
            return !b0_is_former*b0_adjust_pos; 
        };
        
        auto begin = adjust_pos(b0,-sizeof(value_type));
        auto end   = adjust_pos(b1,+sizeof(value_type));
        
        
        
        
        
    }
    
    
    
    
    
    
    
    
    return 0;
}

#endif
