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


template<typename preference_t>
struct gap_iterator{


    using value_type = typename preference_t::value_type;
    using offset_type = typename preference_t::offset_type;

    gap_iterator(preference_t * pref) : pref(pref){}
    ~gap_iterator(){}

    void initialize(value_type from, value_type to){
        auto bt = kautil::algorithm::btree_search{pref};
        auto max_pos = pref->size();
        auto min_pos = 0;
        ovf_state = 0;

        auto b0 = bt.search(from,false);
        auto b1 = bt.search(to,false);

        constexpr auto kBothOvfSame=2;
        constexpr auto kBothOvfDifferent=4;
        constexpr auto kEitherOvf=8;
        
        auto b0_is_contaied = is_contained(b0);
        auto b1_is_contaied = is_contained(b1);
        auto block_size = sizeof(value_type)*2;
        auto fsize = pref->size();
        
        
        begin_ = adjust_pos(b0,true,from);
        end_= adjust_pos(b1,false,to);
            {
                auto both_is_ovf = (b0.overflow&b1.overflow);
                auto either_is_ovf = (b0.overflow^b1.overflow);
                auto both_is_the_same=(b0.nearest_pos == b1.nearest_pos);
                ovf_state|=kBothOvfSame*(both_is_ovf&both_is_the_same);
                ovf_state|=kBothOvfDifferent*(both_is_ovf&!both_is_the_same);
                ovf_state|=kEitherOvf*either_is_ovf;
            }

            auto is_ovf_either = bool(ovf_state&kEitherOvf); 
            auto is_ovf_different = bool(ovf_state&kBothOvfDifferent);
            auto is_ovf_adjust_not_need_itreation = bool(ovf_state&kBothOvfSame);
            
            
            //is_ovf_different
            begin_ = 
                      is_ovf_different*sizeof(value_type)
                    +!is_ovf_different*begin_;
            
            end_ = 
                      is_ovf_different*(max_pos-(sizeof(value_type)*2))
                    +!is_ovf_different*end_;
            
            //is_ovf_either_begin
            begin_ = 
                      is_ovf_either*(!b0.overflow*begin_ +b0.overflow*sizeof(value_type))
                    +!is_ovf_either*begin_;
            end_ = 
                     is_ovf_either*(!b1.overflow*end_ + b1.overflow*(max_pos-sizeof(value_type)))
                   +!is_ovf_either*end_;
            
            // is_same
            begin_*=!is_ovf_adjust_not_need_itreation;
            end_*=!is_ovf_adjust_not_need_itreation;


                
    }





    int ovf_state = 0;
    offset_type begin_ =0;
    offset_type end_ = 0;
    preference_t * pref=0;

};




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
        if(is_even){
            res = b0.nearest_value;
            //pref.read_value(b0.nearest_pos,&ptr);
        }else{
            auto ptr = &res;
            pref.read_value(b0.nearest_pos+sizeof(value_type),&ptr);
        }
    };
    
    auto read_rvalue = [](auto & res,auto & b0,auto & pref){
        auto is_even = !bool(b0.nearest_pos%(sizeof(value_type)*2)); 
        if(is_even){
            auto ptr = &res;
            pref.read_value(b0.nearest_pos+sizeof(value_type),&ptr);
        }else{
            res = b0.nearest_value;
            //pref.read_value(b0.nearest_pos,&ptr);
        }
    };

    {
        
        auto pref = file_syscall_16b_pref{.fd=fileno(f_ranges)};
        auto bt = kautil::algorithm::btree_search{&pref};
        
        auto max_pos = pref.size();
        auto min_pos = 0;
        auto from = value_type(0);auto to = value_type(0);
        {
            from = 0;to = 0; // both ovf(l) expect 0,0 
            from = 2000;to = 2005; // both ovf(u) expect 2000,2005
            from = 0;to = 2005; // both ovf(differ) expect ?2000,2005 // todo : imcomplete 
            from = 0;to = 25; // either ovf(l) expect 0 10 
            from = 26;to = 34; // either ovf(l) expect 0 10 
            from = 0;to = 15; // either ovf(l) expect 0 10   
            from = 0;to = 40; // either ovf(l)  
            from = 15;to = 2005; // either ovf(u) 
            from = 25;to = 2005; // either ovf(u)  
            from = 5;to = 890; // either ovf(u)  
            printf("from,to(%lld,%lld)\n",from,to);fflush(stdout);
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

            auto is_ovf_either = bool(ovf_state&kEitherOvf); 
            auto is_ovf_different = bool(ovf_state&kBothOvfDifferent);
            auto is_ovf_adjust_not_need_itreation = bool(ovf_state&kBothOvfSame);
            
            
            //is_ovf_different
            begin = 
                      is_ovf_different*sizeof(value_type)
                    +!is_ovf_different*begin;
            
            end = 
                      is_ovf_different*(max_pos-(sizeof(value_type)*2))
                    +!is_ovf_different*end;
            
            //is_ovf_either_begin
            begin = 
                      is_ovf_either*(!b0.overflow*begin +b0.overflow*sizeof(value_type))
                    +!is_ovf_either*begin;
            end = 
                     is_ovf_either*(!b1.overflow*end + b1.overflow*(max_pos-sizeof(value_type)))
                   +!is_ovf_either*end;
            
            // is_same
            begin*=!is_ovf_adjust_not_need_itreation;
            end*=!is_ovf_adjust_not_need_itreation;

            
            {// iterate
                
                struct tmp_iterator{
                    value_type l =0;
                    value_type r =0;
                } cur;
                
                auto l_adj = false;
                auto r_adj = false;
                
                if(ovf_state&kBothOvfSame){
                    printf("kBothOvfSame\n");fflush(stdout);
                }else if(ovf_state&kBothOvfDifferent){
                    printf("kBothOvfDifferent\n");fflush(stdout);
                    /* todo : express first(from,arr[0]) and last(arr[last],to)*/
                }else if(ovf_state&kEitherOvf){
                    printf("kEitherOvf\n");fflush(stdout);
                    {
                        // b0 : l(from) r(region)
                        // b1 : l(region) r(to)
                        
                        {// decide to which(l or r) the value is loaded 
                            auto p = reinterpret_cast<value_type*>(
                                     b0.overflow*uintptr_t(&cur.r)
                                    +b1.overflow*uintptr_t(&cur.l));
                            auto pos_pol = b0.overflow*min_pos+b1.overflow*(max_pos-sizeof(value_type));
                            pref.read_value(pos_pol,&p);
                        }
                        
                        {// decide to which(l or r) input value is assigned
                            auto input_p = reinterpret_cast<value_type*>(
                                     b0.overflow*uintptr_t(&cur.l)
                                    +b1.overflow*uintptr_t(&cur.r));
                            *input_p = b0.overflow*from + b1.overflow*to; 
                        }
                    }
                    
                    l_adj = !b0.overflow*!b0_is_contaied;
                    r_adj = !b1.overflow*!b1_is_contaied;
                    
                    printf("begin,end : %ld,%ld\n",begin,end); fflush(stdout);
                    printf("virtual element : l,r{%d,%d} pole(%lld,%lld)\n",b0.overflow,b1.overflow,cur.l,cur.r);
                    fflush(stdout);
                }
                else{
                    auto block_size = static_cast<offset_type>((sizeof(value_type)*2));
                    l_adj = !b0_is_contaied;
                    r_adj = !b1_is_contaied;
                    
                    auto b0_ignore = b0.overflow;
                    auto b1_ignore = b1.overflow;
                    
                    printf("begin,end : %ld,%ld\n",begin,end); fflush(stdout);
                }
                
                
                
                if(!(ovf_state&kBothOvfSame)){
                
                    auto block_size = sizeof(value_type)*2;
                    auto b0_ignore = b0.overflow;
                    auto b1_ignore = b1.overflow;
                    
                    // if contaied then round(b0_np,sizeof(value_type)*2) == round(b1_np,sizeof(value_type)*2)
                    // if not contaied  round(b0_np-sizeof(vt),sizeof(value_type)*2) == round(b1_np-sizeof(vt),sizeof(value_type)*2)
                    auto b0_belongs_to = (b0.nearest_pos-(!b0_is_contaied*sizeof(value_type)))/block_size*block_size; 
                    auto b1_belongs_to = (b1.nearest_pos-(!b1_is_contaied*sizeof(value_type)))/block_size*block_size; 
                    
                    // adjust b1_ignore
                        // the condition under which counter is ignored onece in a foreach (the condition of two times ignore is not concern).   
                    b1_ignore =
                        !(
                             (b0_ignore&b1_ignore)
                            &(b0_belongs_to==b1_belongs_to)
                        );
    
    
                    {
                        if((begin==end) & !b1_is_contaied){
                            auto value_ptr = &cur.l;
                            pref.read_value(begin,&value_ptr);
                            cur.r=to;
                            printf("virtual element : l,r{%d,%d} pole(%lld,%lld)\n",b0.overflow,b1.overflow,cur.l,cur.r);
                        }
                    }
                    
                    // if both are belongs to the same block, then ignore count should be 1. 
                    for(auto cur = begin; /*b0_ignore|*/(cur < end); cur+=block_size){
                        auto value = value_type(0);
                        auto value_ptr = &value;
                        printf("begin,end");
                        pref.read_value(cur,&value_ptr);
                        printf("(%ld,",l_adj*from+!l_adj*value);
                        pref.read_value(cur+sizeof(value_type),&value_ptr);
                        auto adjust_value = (r_adj&(cur+block_size >= end));
                        printf("%ld)\n",adjust_value*to + !adjust_value*value);
                        fflush(stdout);
                        l_adj=false;
                    }
                }
            }// iterate
        }// begin , end
        
        
        
//        auto gap_iterator=[](auto from,auto to,auto pref){
//            auto bt = kautil::algorithm::btree_search{&pref};
//            auto b0 = bt.search(from,false);
//            auto b1 = bt.search(to,false);
//        };

        
        
        
        
        
        
        
        
        
        
        
    }
    
    
    
    
    
    
    
    
    return 0;
}

#endif
