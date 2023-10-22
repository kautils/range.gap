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
        
        
        auto max_pos = pref.size();
        auto min_pos = 0;
        
        auto from = value_type(0);auto to = value_type(0);
        from = 0;to = 0;
        from = 0;to = 5;
        from = 2000;to = 2005;
        from = 5;to = 15;
        
        
        auto b0 = bt.search(from,false);
        auto b1 = bt.search(to,false);
        
        
        auto begin = offset_type (0),end = offset_type(0);
        {// adjusting pos 
            auto adjust_pos = [](auto & b0,bool is_from) -> offset_type {
                auto block_size =(sizeof(value_type)*2);
                auto b0_nearest_is_former = !bool(b0.nearest_pos % block_size);
                return static_cast<offset_type>(
                    !(b0.overflow|b0.nan)*(
                         ((is_from &  b0_nearest_is_former &(b0.direction >= 0)) * (b0.nearest_pos+(sizeof(value_type))))
                        +((is_from &  !b0_nearest_is_former &(b0.direction <= 0)) *  b0.nearest_pos)
                        +((!is_from & b0_nearest_is_former &(b0.direction >= 0))* (b0.nearest_pos+(-sizeof(value_type))))
                        +((!is_from & !b0_nearest_is_former &(b0.direction <= 0))*  b0.nearest_pos)
                    )
                );
            };
            // contained or not contained 
            
            auto is_contained = [](auto & b0){
                auto block_size =(sizeof(value_type)*2);
                auto b0_nearest_is_former = !bool(b0.nearest_pos % block_size);
                auto b0_cond_not_contained = 
                         (b0_nearest_is_former&(b0.direction < 0))
                        |(!b0_nearest_is_former&(b0.direction > 0));
                return !(b0_cond_not_contained|b0.overflow);
            };
            auto b0_is_contaied = is_contained(b0);
            auto b1_is_contaied = is_contained(b1);
            
            
            auto left_v = 1234; // left pole value
            auto right_v = 4567;// right pole value
            from = 
                    !b0_is_contaied*from
                    +b0_is_contaied*right_v;
            to = 
                    !b1_is_contaied*to
                    +b1_is_contaied*left_v;
            
            
            auto fsize = pref.size();
            begin = adjust_pos(b0,true);
            end   = adjust_pos(b1,false);
            
            printf("begin,end : %ld,%ld\n",begin,end); fflush(stdout);
            
            auto ovf_cnt = int(0);{
                auto calc_ovf_count = [](auto & b0,bool is_from)->int{
                    // is_from & overflow(f or l)
                        // ovf(f) : ovf_count+=2
                        // ovf(l) : ovf_count+=1 : ignore
                    // !is_from & overflow(f or l)
                        // ovf(f) : ovf_count+=1 : ignore 
                        // ovf(l) : ovf_count+=2
                    return 
                         -2*(is_from & b0.overflow & (b0.direction < 0))
                        +2*(!is_from & b0.overflow & (b0.direction > 0)); 
                };
                ovf_cnt+=calc_ovf_count(b0,true);
                ovf_cnt+=calc_ovf_count(b1,false);
                printf("ovf_cnt : %d\n",ovf_cnt); fflush(stdout);
            }// ovf_cnt
            
            {// iterate
                auto block_size = static_cast<offset_type>((sizeof(value_type)*2));
                if(ovf_cnt < 0){
                    printf("ovf(l) begin,end(%ld,%ld)\n",from,to);
                }
                for(auto cur = begin; cur < end; cur+=block_size){
                    printf("%ld begin,end(%ld,%ld)\n",cur,begin,end); fflush(stdout);
                }
                if(ovf_cnt > 0)printf("ovf(u) begin,end(%ld,%ld)\n",from,to);
                // next
                // is_end
            }
            
        }
        
        
        
    }
    
    
    
    
    
    
    
    
    return 0;
}

#endif
