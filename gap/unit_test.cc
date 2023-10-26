#ifdef TMAIN_KAUTIL_RANGE_GAP_INTERFACE



#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>


#include "gap.hpp"

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


template<typename pref_t> struct gap2;

template<typename pref_t>
struct gap2_iterator{
    
    
    using value_type = typename pref_t::value_type;
    using offset_type = typename pref_t::offset_type;
    
    using self_type = gap2_iterator; 
    
    gap2_iterator(){}
    ~gap2_iterator(){}
    
    
    struct current{ value_type r;value_type l; };
    self_type & operator++(){
        ++cur;
        return *this;
    }
    
    bool operator!=(self_type & l){ return cur != l.cur; }
    current operator*(){ 
        auto res = current{};
        auto pos = cur*p->kBlockSize-sizeof(value_type);
        
        if( (cur == vp_l) | (cur == -1) ){
            res.l = from;
        }else{
            auto ptr = &res.l;
            p->pref->read_value(pos,&ptr);
        }
        
        if( (cur == vp_r) | (cur == -1)){
            res.r = to;
        }else{
            auto ptr = &res.r;
            p->pref->read_value(pos+sizeof(value_type),&ptr);
        }
        return res;
    }
    
    self_type begin() {
        auto res = *this;
        res.cur = p->begin_idx;
        res.vp_l=p->vp_l;
        res.vp_r=p->vp_r;
        res.from=p->from;
        res.to=p->to;
        return res;
    }   
    
    self_type end() {
        auto res = *this;
        res.cur = p->end_idx;
        return res;
    }    
    
    offset_type vp_l=0;
    offset_type vp_r=0;
    offset_type from=0;
    offset_type to=0;
    offset_type cur=0;
    gap2<pref_t>* p=0;
};

template<typename pref_t>
struct gap2{
    
    friend struct gap2_iterator<pref_t>;
    
    using value_type = typename pref_t::value_type;
    using offset_type = typename pref_t::offset_type;
    
    gap2(pref_t * pref) : pref(pref){}
    ~gap2(){}
    
    static constexpr offset_type kBlockSize = sizeof(value_type)*2;
    
    int initialize(value_type f,value_type t){
        
        auto is_contained = [](auto & b0){
            auto block_size =(sizeof(value_type)*2);
            auto b0_nearest_is_former = !bool(b0.nearest_pos % block_size);
            auto b0_cond_not_contained = 
                     (b0_nearest_is_former&(b0.direction < 0))
                    |(!b0_nearest_is_former&(b0.direction > 0));
            return !(b0_cond_not_contained|b0.overflow);
        };
        
        
        from = f;
        to = t;
        
        auto bt = kautil::algorithm::btree_search{pref};
        auto b0 = bt.search(from,false);
        auto b0_is_contained = is_contained(b0);
        auto b0_is_even =!bool(b0.nearest_pos%kBlockSize);
        b0.nearest_pos = 
                 b0.overflow*-sizeof(value_type)
               +!b0.overflow*(
                    b0_is_contained*(
                         !b0_is_even*b0.nearest_pos
                        + b0_is_even*(b0.nearest_pos+sizeof(value_type)) 
                    )
                  +!b0_is_contained*( 
                         !b0_is_even*(b0.nearest_pos) 
                        + b0_is_even*(b0.nearest_pos-sizeof(value_type)) 
                  )
               );
        
        
        auto b1 = bt.search(to,false);
        auto b1_is_contained = is_contained(b1);
        auto b1_is_even =!bool(b1.nearest_pos%kBlockSize);
        b1.nearest_pos = 
                 b1.overflow*(pref->size()-sizeof(value_type)) 
               +!b1.overflow*(
                   b1_is_contained*(
                         !b1_is_even*(b1.nearest_pos-kBlockSize) 
                        + b1_is_even*(b1.nearest_pos-sizeof(value_type))
                    )
                  +!b1_is_contained*(
                         !b1_is_even*(b1.nearest_pos)
                        + b1_is_even*(b1.nearest_pos-sizeof(value_type))
                  )  
               );
        
        
        auto cond_ovfovf_same = (b0.overflow&b1.overflow)&(b0.direction==b1.direction);
        begin_idx = 
                  cond_ovfovf_same*-1
                +!cond_ovfovf_same*static_cast<offset_type>(b0.nearest_pos+sizeof(value_type))/kBlockSize;
        end_idx = 
                  cond_ovfovf_same*-1
                +!cond_ovfovf_same*(static_cast<offset_type>(b1.nearest_pos+sizeof(value_type))/kBlockSize);
        
        constexpr auto kVirtualNPos = offset_type(-3); 
        vp_l =
                static_cast<offset_type>(
                      cond_ovfovf_same*(-1)
                    +!cond_ovfovf_same*(
                          b0.overflow*(0)
                        +!b0.overflow*(
                          !b0_is_contained*begin_idx
                          +b0_is_contained*(kVirtualNPos)
                        )
                    )
                );

        vp_r =
                static_cast<offset_type>(
                      cond_ovfovf_same*(-1)
                    +!cond_ovfovf_same*(
                          b1.overflow*(pref->size()/kBlockSize)
                        +!b1.overflow*(
                          !b1_is_contained*end_idx
                          +b1_is_contained*(kVirtualNPos)
                        )
                    )
                );
        
        end_idx+=1;
        
        
        return 0;
    }
    
    
    
    
    
    
    using self_type = gap2<pref_t>;
    using iterator_type = gap2_iterator<pref_t>;
    
    iterator_type iterator(){
        auto res = iterator_type{};
        res.p = this;
        return res;
    }
    
    


private:
    value_type from=0,to=0;
    offset_type cur=0;
    offset_type begin_idx=0;
    offset_type end_idx=0;
    offset_type vp_l=0;
    offset_type vp_r=0;
    
    
    pref_t * pref=0;
};



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
//    {
        auto pref = file_syscall_16b_pref{.fd=fileno(f_ranges)};
        auto bt = kautil::algorithm::btree_search{&pref};
        
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
            
            printf("from,to(%lld,%lld)\n",from,to);fflush(stdout);
        }
        
        auto b0 = bt.search(from,false);
        auto b0_is_contained = is_contained(b0);
        auto b0_is_even =!bool(b0.nearest_pos%kBlockSize);
        b0.nearest_pos = 
                 b0.overflow*-sizeof(value_type)
               +!b0.overflow*(
                    b0_is_contained*(
                         !b0_is_even*b0.nearest_pos
                        + b0_is_even*(b0.nearest_pos+sizeof(value_type)) 
                    )
                  +!b0_is_contained*( 
                         !b0_is_even*(b0.nearest_pos) 
                        + b0_is_even*(b0.nearest_pos-sizeof(value_type)) 
                  )
               );
        
        
        auto b1 = bt.search(to,false);
        auto b1_is_contained = is_contained(b1);
        auto b1_is_even =!bool(b1.nearest_pos%kBlockSize);
        b1.nearest_pos = 
                 b1.overflow*(pref.size()-sizeof(value_type)) 
               +!b1.overflow*(
                   b1_is_contained*(
                         !b1_is_even*(b1.nearest_pos-kBlockSize) 
                        + b1_is_even*(b1.nearest_pos-sizeof(value_type))
                    )
                  +!b1_is_contained*(
                         !b1_is_even*(b1.nearest_pos)
                        + b1_is_even*(b1.nearest_pos-sizeof(value_type))
                  )  
               );
        
        
        
        auto cond_ovfovf_same = (b0.overflow&b1.overflow)&(b0.direction==b1.direction);
        auto b0_idx = 
                  cond_ovfovf_same*-1
                +!cond_ovfovf_same*static_cast<offset_type>(b0.nearest_pos+sizeof(value_type))/kBlockSize;
        auto b1_idx = 
                  cond_ovfovf_same*-1
                +!cond_ovfovf_same*static_cast<offset_type>(b1.nearest_pos+sizeof(value_type))/kBlockSize;
        
        constexpr auto kVirtualNPos = offset_type(-3); 
        auto b0_virtual_pos =
                static_cast<offset_type>(
                      cond_ovfovf_same*(-1)
                    +!cond_ovfovf_same*(
                          b0.overflow*(0)
                        +!b0.overflow*(
                          !b0_is_contained*b0_idx
                          +b0_is_contained*(kVirtualNPos)
                        )
                    )
                );

        auto b1_virtual_pos =
                static_cast<offset_type>(
                      cond_ovfovf_same*(-1)
                    +!cond_ovfovf_same*(
                          b1.overflow*(pref.size()/kBlockSize)
                        +!b1.overflow*(
                          !b1_is_contained*b1_idx
                          +b1_is_contained*(kVirtualNPos)
                        )
                    )
                );
        
        
        
    
        auto begin = offset_type(0);
        auto end = offset_type(0);
        auto cur = offset_type(0);
        {// when below condition are satisfied then virtual pos is returned. 
            b0.overflow&!b0_is_contained&(cur == b0_idx);
            b1.overflow&!b1_is_contained&(cur == b1_idx);
            cond_ovfovf_same&(cur == begin);
        }
    
        
        printf("np (%ld %ld)\n ",b0.nearest_pos,b1.nearest_pos);
        printf("idx(%ld %ld)\n ",b0_idx,b1_idx);
        printf("vp (%ld %ld)\n ",b0_virtual_pos,b1_virtual_pos);
        printf("b,e(%ld %ld)\n ",b0_idx,b1_idx+1);
        printf("\n");
        fflush(stdout);        
        
    
        
    auto gp = gap2{&pref};
    gp.initialize(from,to);
    auto itr = gp.iterator();
    for(auto const& elem : gp.iterator() ){
        
        printf("l,r(%lld,%lld)\n",elem.l,elem.r);
        fflush(stdout);
        
        
    }
    
        
        
    
    
    
    
    
    
    return 0;
}

#endif
