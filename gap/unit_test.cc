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
    {
        auto written = fwrite(data.data(),sizeof(value_type),data.size(),f_ranges);
        fflush(f_ranges);
        
        auto fd = fileno(f_ranges);
        printf("file size : %ld\n",lseek(fd,0,SEEK_END)-lseek(fd,0,SEEK_SET));
    }
    
    
    auto adjust_pos = [](auto & b0,bool is_from,auto const& current_value) -> offset_type {
        auto block_size =(sizeof(value_type)*2);
        auto b0_nearest_is_former = !bool(b0.nearest_pos % block_size);
        return static_cast<offset_type>(
            
            //from
            // contained
             ((is_from &  b0_nearest_is_former &(b0.direction >= 0)) * (b0.nearest_pos+(sizeof(value_type))))
            +((is_from &  !b0_nearest_is_former &(b0.direction <= 0)) *  b0.nearest_pos)
            // !contained
            +((is_from &  b0_nearest_is_former &!(b0.direction >= 0)) * (b0.nearest_pos-(sizeof(value_type))))
            +((is_from &  !b0_nearest_is_former &!(b0.direction <= 0)) *  b0.nearest_pos)
             
            // to
            // contained
            +((!is_from & b0_nearest_is_former  & (b0.direction >= 0))* (b0.nearest_pos))
            +((!is_from & !b0_nearest_is_former & (b0.direction <= 0))*  b0.nearest_pos)+(-sizeof(value_type))
            // !contained
            +((!is_from & b0_nearest_is_former  &!(b0.direction >= 0))* (b0.nearest_pos))
            +((!is_from & !b0_nearest_is_former &!(b0.direction <= 0))*  b0.nearest_pos)+(+sizeof(value_type))
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
    
    auto read_lvalue = [](auto & res,auto & b0,auto & pref){
        auto is_even = !bool(b0.nearest_pos%(sizeof(value_type)*2)); 
        auto ptr = &res;
        if(is_even){
            pref.read_value(b0.nearest_pos,&ptr);
        }else{
            pref.read_value(b0.nearest_pos+sizeof(value_type),&ptr);
        }
    };
    
    auto read_rvalue = [](auto & res,auto & b0,auto & pref){
        auto is_even = !bool(b0.nearest_pos%(sizeof(value_type)*2)); 
        auto ptr = &res;
        if(is_even){
            pref.read_value(b0.nearest_pos+sizeof(value_type),&ptr);
        }else{
            pref.read_value(b0.nearest_pos,&ptr);
        }
    };

    auto calc_ovf_count = [](auto & b0,bool is_from)->int{
        // is_from & overflow(f or l)
            // ovf(f) : ovf_count+=2
            // ovf(l) : ignore
        // !is_from & overflow(f or l)
            // ovf(f) : ignore 
            // ovf(l) : ovf_count+=2
        return 
             -2*(is_from & b0.overflow & (b0.direction < 0))
            +2*(!is_from & b0.overflow & (b0.direction > 0)); 
    };

    
    {
        
        auto pref = file_syscall_16b_pref{.fd=fileno(f_ranges)};
        auto bt = kautil::algorithm::btree_search{&pref};
        
        auto max_pos = pref.size();
        auto min_pos = 0;
        auto from = value_type(0);auto to = value_type(0);
        {
            from = 0;to = 0; // expect 0,0
            from = 2000;to = 2005; // expect 2000,2005
            from = 0;to = 2005; // expect 2000,2005
            from = 20;to = 30; // expect 20,30
            from = 25;to = 30; // expect 25,30
//            from = 5;to = 15;// expect 5,10
//            from = 10;to = 20; // expect nothing
//            from = 20;to = 30; // expect 20,30
        }
        
        auto b0 = bt.search(from,false);
        auto b1 = bt.search(to,false);
        
        
        constexpr auto kBothOvfSame=2;
        constexpr auto kBothOvfDifferent=4;
        constexpr auto kEitherOvf=8;
        int ovf_state = 0;
        auto begin = offset_type (0),end = offset_type(0);{ // begin , end 

            auto b0_is_contaied = is_contained(b0);
            auto b1_is_contaied = is_contained(b1);
            
            if(b0_is_contaied) read_rvalue(from,b0,pref);
            if(b1_is_contaied) read_lvalue(to,b1,pref);
            
            auto fsize = pref.size();
            begin = adjust_pos(b0,true,from);
            end   = adjust_pos(b1,false,to);
            
            
            {
                auto both_is_ovf = (b0.overflow&b1.overflow);
                auto either_is_ovf = (b0.overflow^b1.overflow);
                auto both_is_the_same=(b0.nearest_pos == b1.nearest_pos);
                ovf_state|=kBothOvfSame*(both_is_ovf&both_is_the_same);
                ovf_state|=kBothOvfDifferent*(both_is_ovf&!both_is_the_same);
                ovf_state|=kEitherOvf*either_is_ovf;
            }
            
            auto adjust_begin = bool(
                  min_pos*(ovf_state&kBothOvfDifferent)
                + min_pos*(ovf_state&kEitherOvf)&b0.overflow
            ); 
            
            auto adjust_end = bool(
                  max_pos*(ovf_state&kBothOvfDifferent)
                + max_pos*(ovf_state&kEitherOvf)&b1.overflow
            ); 
            
            begin = 
                    adjust_begin*(
                          min_pos*(ovf_state&kBothOvfDifferent)
                        + min_pos*(ovf_state&kEitherOvf)&b0.overflow
                    ) 
                    +!adjust_begin*begin;
            
            end =
                    adjust_end*(
                          max_pos*(ovf_state&kBothOvfDifferent)
                        + max_pos*(ovf_state&kEitherOvf)&b1.overflow
                    )
                    +!adjust_end*end;
            
            
            
            printf("begin,end : %ld,%ld\n",begin,end); fflush(stdout);
            
            auto ovf_cnt = int(0);{
                ovf_cnt+=calc_ovf_count(b0,true);
                ovf_cnt+=calc_ovf_count(b1,false);
                printf("ovf_cnt : %d\n",ovf_cnt); fflush(stdout);
            }// ovf_cnt
            
            
            {// iterate
                
                if(ovf_state&kBothOvfSame){
                    printf("kBothOvfSame\n");fflush(stdout);
                }else if(ovf_state&kBothOvfDifferent){
                    printf("kBothOvfDifferent\n");fflush(stdout);
                }else{
                    auto block_size = static_cast<offset_type>((sizeof(value_type)*2));
                    auto l_adj = !b0_is_contaied;
                    auto r_adj = !b1_is_contaied;
                    for(auto cur = begin; cur < end; cur+=block_size){
                        auto value = value_type(0);
                        auto value_ptr = &value;
                        printf("begin,end");
                        pref.read_value(cur,&value_ptr);
                        printf("(%ld,",l_adj*from+!l_adj*value);
                        pref.read_value(cur+sizeof(value_type),&value_ptr);
                        printf("%ld)\n",value);
                        fflush(stdout);
                        l_adj=false;
                    }
                }
                
                
            }// iterate
        }// begin , end
        
        
    }
    
    
    
    
    
    
    
    
    return 0;
}

#endif
