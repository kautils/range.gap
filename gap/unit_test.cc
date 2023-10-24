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

//template<typename preference_t>
//struct gap_iterator{
//    using offset_type = typename preference_t::offset_type;
//    using value_type = typename preference_t::value_type;
//    using self_type = gap_iterator; 
//    gap_iterator(){}
//    
//    self_type begin(){ return self_type{}; }
//    self_type end(){ return self_type{}; }
//    self_type & operator++(self_type &){ return this; }
//    
//    
//    offset_type cur;
//};


template<typename preference_t>
struct gap{

    using self_type = gap;
    using value_type = typename preference_t::value_type;
    using offset_type = typename preference_t::offset_type;

    gap(preference_t * pref) : pref(pref){}
    ~gap(){}

    static constexpr auto kBothOvfSame=2;
    static constexpr auto kBothOvfDifferent=4;
    static constexpr auto kEitherOvfLeft=8;
    static constexpr auto kEitherOvfRight=16;
    static constexpr auto kBlockSize =(sizeof(value_type)*2);
    
    void initialize(value_type __from, value_type __to){
        
        
        auto is_contained = [](auto & b0){
            auto b0_nearest_is_former = !bool(b0.nearest_pos % kBlockSize);
            auto b0_cond_not_contained = 
                     (b0_nearest_is_former&(b0.direction < 0))
                    |(!b0_nearest_is_former&(b0.direction > 0));
            return !(b0_cond_not_contained|b0.overflow);
        };
    
        
        auto adjust_pos = [](auto & b0,bool is_from,auto const& current_value) -> offset_type {
            auto b0_nearest_is_former = !bool(b0.nearest_pos % kBlockSize);
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
                +((!is_from & b0_nearest_is_former  & (b0.direction >= 0))  * (b0.nearest_pos))
                +((!is_from & !b0_nearest_is_former & (b0.direction <= 0))) * (b0.nearest_pos-sizeof(value_type))
                // !contained
                +((!is_from & b0_nearest_is_former  &!(b0.direction >= 0))  * (b0.nearest_pos))
                +((!is_from & !b0_nearest_is_former &!(b0.direction <= 0))) * (b0.nearest_pos+sizeof(value_type))
            );
        };
        
        
        auto bt = kautil::algorithm::btree_search{pref};
        auto max_pos = pref->size();
        auto min_pos = 0;
        
        from = __from;
        to = __to;
        ovf_state = 0;

        auto b0 = bt.search(from,false);
        auto b1 = bt.search(to,false);

        auto b0_is_contaied = is_contained(b0);
        auto b1_is_contaied = is_contained(b1);
        auto fsize = pref->size();
        
        
        begin_ = adjust_pos(b0,true,from);
        end_= adjust_pos(b1,false,to);
        
        auto is_ovf_either = (b0.overflow^b1.overflow);
        auto is_ovf_both_same= (b0.overflow&b1.overflow)&(b0.nearest_pos == b1.nearest_pos);
        auto is_ovf_both_different = (b0.overflow&b1.overflow)&!is_ovf_both_same;
        {
            ovf_state=
                  kBothOvfSame*is_ovf_both_same
                 +kBothOvfDifferent*is_ovf_both_different
                 +kEitherOvfLeft *(is_ovf_either*b0.overflow)
                 +kEitherOvfRight*(is_ovf_either*b1.overflow);
        }

        //is_ovf_both_different
        begin_ = 
                  is_ovf_both_different*sizeof(value_type)
                +!is_ovf_both_different*begin_;
        
        end_ = 
                  is_ovf_both_different*(max_pos-(kBlockSize))
                +!is_ovf_both_different*end_;
        
        //is_ovf_either_begin
        begin_ = 
                  is_ovf_either*(!b0.overflow*begin_ +b0.overflow*sizeof(value_type))
                +!is_ovf_either*begin_;
        end_ = 
                 is_ovf_either*(!b1.overflow*end_ + b1.overflow*(max_pos-sizeof(value_type)))
               +!is_ovf_either*end_;
        
        // is_same
        begin_*=!is_ovf_both_same;
        end_*=!is_ovf_both_same;
            
    
        l_adj = false;
        r_adj = false;
    
        l_adj = 
                  is_ovf_either*(!b0.overflow*!b0_is_contaied)
                +!is_ovf_either*l_adj;
        r_adj = 
                  is_ovf_either*(!b1.overflow*!b1_is_contaied)
                +!is_ovf_either*r_adj;
        
        l_adj = 
                !ovf_state*!b0_is_contaied
                +ovf_state*l_adj;
        r_adj = 
                !ovf_state*!b1_is_contaied
                +ovf_state*r_adj;
    }
    
    struct current{ value_type l =0;value_type r =0; }; 
    self_type begin(){ 
        auto res = *this; 
        res.cur = begin_;
        return res;
    }
    
    self_type end(){ 
        auto res = *this; 
        res.cur = end_;
        return res;
    }
    self_type & operator++(){ 
        
    
        auto lmb_virtual_value = [](auto from,auto to,bool lr, auto * pref){
            auto cur = current{};
            auto max_pos = pref->size();
            auto min_pos = 0;
            {
                // b0 : l(from) r(region)
                // b1 : l(region) r(to)
                {// decide to which(l or r) the value is loaded 
                    auto p = reinterpret_cast<value_type*>(
                             lr*uintptr_t(&cur.r)
                            +!lr*uintptr_t(&cur.l));
                    auto pos_pol = lr*min_pos+!lr*static_cast<offset_type>(max_pos-sizeof(value_type));
                    pref->read_value(pos_pol,&p);
                }
                
                {// decide to which(l or r) input value is assigned
                    auto input_p = reinterpret_cast<value_type*>(
                             lr*uintptr_t(&cur.l)
                            +!lr*uintptr_t(&cur.r));
                    *input_p = lr*from + !lr*to; 
                }
//                printf("virtual element : l,r{%d,%d} pole(%lld,%lld)\n",lr,!lr,cur.l,cur.r);
//                fflush(stdout);
            }
        };
        
//        if(ovf_state&kBothOvfSame){
//            //printf("kBothOvfSame\n");fflush(stdout);
//        }else if(ovf_state&kBothOvfDifferent){
////            printf("kBothOvfDifferent\n");fflush(stdout);
//            lmb_virtual_value(from,to,true,pref);
//            lmb_virtual_value(from,to,false,pref);
//        }else if(ovf_state&kEitherOvf){
////            printf("kEitherOvf\n");fflush(stdout);
////            lmb_virtual_value(from,to,b0.overflow,&pref);
//        }
        
        // if ovf b0 & first
        // if ovf b1 & last
        // then ignore once
        cur += kBlockSize;
        return *this; 
    }
    current operator*() { 
//        constexpr auto kBlockSize =(sizeof(value_type)*2);
        auto res=current{};
        auto value_ptr = (value_type*) 0;
        pref->read_value(cur,&(value_ptr = &res.l));
        pref->read_value(cur+sizeof(value_type),&(value_ptr = &res.r));
        auto adjust_r = (r_adj&(cur+(kBlockSize) >= end_)); // add condition to detect last one
        l_adj=false; // only first time is concerned if it is true
        return res;
    }
    
    bool operator!=(self_type & r){
        return (cur != r.cur) &(cur != r.cur+sizeof(value_type));
    }
    
    
    bool l_adj = false;
    bool r_adj = false;

    int ovf_state = 0; // 0 : nothing ovf, 2 : ovf both,same,  4 : ovf both,different, 8 : ovf either
    offset_type cur = 0;
    offset_type begin_ =0;
    offset_type end_ = 0;
    value_type from=0;
    value_type to=0;
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
            +((!is_from & b0_nearest_is_former  & (b0.direction >= 0))  * (b0.nearest_pos))
            +((!is_from & !b0_nearest_is_former & (b0.direction <= 0))) * (b0.nearest_pos-sizeof(value_type))
            // !contained
            +((!is_from & b0_nearest_is_former  &!(b0.direction >= 0))  * (b0.nearest_pos))
            +((!is_from & !b0_nearest_is_former &!(b0.direction <= 0))) * (b0.nearest_pos+sizeof(value_type))
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
    
    auto read_lvalue = [](auto & value,auto & b0,auto & pref){
        auto is_even = !bool(b0.nearest_pos%(sizeof(value_type)*2)); 
        if(is_even){
            value = b0.nearest_value;
            //pref.read_value(b0.nearest_pos,&ptr);
        }else{
            auto ptr = &value;
            pref.read_value(b0.nearest_pos+sizeof(value_type),&ptr);
        }
    };
    
    auto read_rvalue = [](auto & value,auto & b0,auto & pref){
        auto is_even = !bool(b0.nearest_pos%(sizeof(value_type)*2)); 
        if(is_even){
            auto ptr = &value;
            pref.read_value(b0.nearest_pos+sizeof(value_type),&ptr);
        }else{
            value = b0.nearest_value;
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
            from = 0;to = 2005; // both ovf(differ) expect ?2000,2005  
            from = 0;to = 24; // either ovf(l) expect 0 10 
            from = 26;to = 34; // either ovf(l) expect 0 10 
            from = 26;to = 45; // either ovf(l) expect 0 10 
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
                auto is_ovf_both_same=(b0.nearest_pos == b1.nearest_pos);
                ovf_state|=kBothOvfSame*(both_is_ovf&is_ovf_both_same);
                ovf_state|=kBothOvfDifferent*(both_is_ovf&!is_ovf_both_same);
                ovf_state|=kEitherOvf*either_is_ovf;
            }

            auto is_ovf_either = bool(ovf_state&kEitherOvf); 
            auto is_ovf_both_different = bool(ovf_state&kBothOvfDifferent);
            auto is_ovf_adjust_not_need_itreation = bool(ovf_state&kBothOvfSame);
            
            
            //is_ovf_both_different
            begin = 
                      is_ovf_both_different*sizeof(value_type)
                    +!is_ovf_both_different*begin;
            
            end = 
                      is_ovf_both_different*(max_pos-(sizeof(value_type)*2))
                    +!is_ovf_both_different*end;
            
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

            
            
            auto l_adj = false;
            auto r_adj = false;
            l_adj = 
                      is_ovf_either*(!b0.overflow*!b0_is_contaied)
                    +!is_ovf_either*l_adj;
            r_adj = 
                      is_ovf_either*(!b1.overflow*!b1_is_contaied)
                    +!is_ovf_either*r_adj;
            
            l_adj = 
                    !ovf_state*!b0_is_contaied
                    +ovf_state*l_adj;
            r_adj = 
                    !ovf_state*!b1_is_contaied
                    +ovf_state*r_adj;
            
            
            
            auto lmb_virtual_value = [](
                    auto from,auto to
                    ,bool lr 
                    , auto * pref
                    ){
                
                struct tmp_iterator{
                    value_type l =0;
                    value_type r =0;
                } cur;
                
                auto max_pos = pref->size();
                auto min_pos = 0;
                {
                    // b0 : l(from) r(region)
                    // b1 : l(region) r(to)
                    {// decide to which(l or r) the value is loaded 
                        auto p = reinterpret_cast<value_type*>(
                                 lr*uintptr_t(&cur.r)
                                +!lr*uintptr_t(&cur.l));
                        auto pos_pol = lr*min_pos+!lr*static_cast<offset_type>(max_pos-sizeof(value_type));
                        pref->read_value(pos_pol,&p);
                    }
                    
                    {// decide to which(l or r) input value is assigned
                        auto input_p = reinterpret_cast<value_type*>(
                                 lr*uintptr_t(&cur.l)
                                +!lr*uintptr_t(&cur.r));
                        *input_p = lr*from + !lr*to; 
                    }
                    //printf("begin,end : %ld,%ld\n",begin,end); fflush(stdout);
                    printf("virtual element : l,r{%d,%d} pole(%lld,%lld)\n",lr,!lr,cur.l,cur.r);
                    fflush(stdout);
                }
            };
            
            
            {// iterate
                
                // add first
                // add last
                // add first and last
                // only one
                
                
                struct tmp_iterator{
                    value_type l =0;
                    value_type r =0;
                } cur;
                
                if(ovf_state&kBothOvfSame){
                    printf("kBothOvfSame\n");fflush(stdout);
                }else if(ovf_state&kBothOvfDifferent){
                    printf("kBothOvfDifferent\n");fflush(stdout);
                    lmb_virtual_value(from,to,true,&pref);
                    lmb_virtual_value(from,to,false,&pref);
                }else if(ovf_state&kEitherOvf){
                    printf("kEitherOvf\n");fflush(stdout);
                    lmb_virtual_value(from,to,b0.overflow,&pref);
                }
                
                
                if(!(ovf_state&kBothOvfSame)){
                    auto l = value_type(0);
                    auto r = value_type(0);
                    auto value_ptr = (value_type*) 0;  
                    constexpr auto block_size = sizeof(value_type)*2;
//                    for(auto cur = begin; cur < end; cur+=block_size){
                    for(auto cur = begin; (cur != end) & (cur != (end+sizeof(value_type)) ) ; cur+=block_size){
                        pref.read_value(cur,&(value_ptr = &l));
                        pref.read_value(cur+sizeof(value_type),&(value_ptr = &r));
                        auto adjust_r = (r_adj&(cur+block_size >= end)); // add condition to detect last one
                        printf("begin,end(%ld,%ld) cur,end(%ld,%ld)\n",l_adj*from+!l_adj*l,adjust_r*to+!adjust_r*r,cur,end);
                        l_adj=false;
                    }
                }
            }// iterate
        }// begin , end
        
        
        auto g = gap{&pref};
        g.initialize(from,to);
        for(auto const& elem : g){
            printf("l,r(%lld,%lld)\n",elem.l,elem.r);
            fflush(stdout);
        }
        
        
    }
    
    
    
    
    
    
    
    
    
    return 0;
}

#endif
