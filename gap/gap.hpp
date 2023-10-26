#ifndef FLOW_RANGE_GAP_GAP_GAP_HPP
#define FLOW_RANGE_GAP_GAP_GAP_HPP

#include "kautil/algorithm/btree_search/btree_search.hpp"


template<typename pref_t>
struct gap{
    
    using value_type = typename pref_t::value_type;
    using offset_type = typename pref_t::offset_type;
    
    gap(pref_t * pref) : pref(pref){}
    ~gap(){}
    
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
        
        // adjust nearest pos of b0 and b1 to sizeof(valuet_type)*n 
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
        
        
        //there is 8 pattern of pos 
        // 1) from is contained by region
        // 2) from is not contained by region
        // 3) to is contained by region
        // 4) to is not contained by region
        // 5) from is overflow
        // 6) to is overflow
        // 7) from and to is overflow, and they are in the left. 
        // 8) from and to is overflow, and they are in the right.
        
        // 1) -> right value of range
        // 2) -> left value of vacant
        // 3) -> left value of range (move forward this for sizeof(value_type) equals to end()) 
        // 4) -> right value of vacant  (move forward this for 1 block equals to end()) 
        // 5) -> 0
        // 6) -> fsize/blockSize
        // 7),8) -> -1 
        
        auto cond_ovfovf_same = (b0.overflow&b1.overflow)&(b0.direction==b1.direction);
        begin_idx = 
                  cond_ovfovf_same*-1
                +!cond_ovfovf_same*static_cast<offset_type>(b0.nearest_pos+sizeof(value_type))/kBlockSize;
        end_idx = 
                  cond_ovfovf_same*-1
                +!cond_ovfovf_same*(static_cast<offset_type>(b1.nearest_pos+sizeof(value_type))/kBlockSize);
        
        
        // virtual pos : if iterator points virtual pos, then input is used as the returned value.   
        // 2),3),5),6),7),8) -> need virtual pos
        
        // 2),3),5),6) as is
        // 7),8) -> -1
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
        
        
        // dbg
        //printf("np (%ld %ld)\n ",b0.nearest_pos,b1.nearest_pos);
        //printf("vp (%ld %ld)\n ",vp_l,vp_r);
        //printf("b,e(%ld %ld)\n ",begin_idx,end_idx);
        //printf("\n");fflush(stdout);        

        
        return 0;
    }
    
    
    using self_type=gap<pref_t>; 
    struct current{ value_type l;value_type r; }__attribute__((__aligned__(8)));
    self_type & operator++(){ ++cur;return *this; }
    self_type & operator--(){ --cur; return *this; }
    self_type operator++(int){ auto res = *this; ++res.cur;return res; }
    self_type operator--(int){ auto res = *this; --res.cur;return res; }
    self_type operator+(offset_type n){ auto res = *this; res.cur+=n;return res; }
    self_type operator-(offset_type n){ auto res = *this; res.cur-=n;return res; }
    self_type & operator+=(offset_type n){ cur+=n;return *this; }
    self_type & operator-=(offset_type n){ cur-=n;return *this; }

    
    bool operator!=(self_type & l){ return cur != l.cur; }
    current operator*(){ 
        auto res = current{};
        // ovf(left)  -> 0
        // ovf(right) -> fsize - sizeof(value_type)
        auto pos = 
                  ((cur==-1)|!cur)*0
                +!((cur==-1)|!cur)*(cur*kBlockSize-sizeof(value_type));

        auto res_ptr = &res;
        pref->read(pos,(void**)&res_ptr,sizeof(current));
        auto l_is_vp = (cur == vp_l) | (cur == -1); 
        auto r_is_vp = (cur == vp_r) | (cur == -1); 

        //res.l = r_is_vp*res.r +!r_is_vp*res.l;  // not need
        res.r = l_is_vp*res.l +!l_is_vp*res.r;

        res.l = 
                  l_is_vp*from
                +!l_is_vp*res.l;
        res.r = 
                  r_is_vp*to
                +!r_is_vp*res.r;
        return res;
    }

    self_type begin() { auto res = *this;res.cur = begin_idx;return res; }   
    self_type end() { auto res = *this;res.cur = end_idx;return res; }    
    
    
private:
    value_type from=0,to=0;
    offset_type cur=0;
    offset_type begin_idx=0;
    offset_type end_idx=0;
    offset_type vp_l=0;
    offset_type vp_r=0;
    
    
    pref_t * pref=0;
};




#endif
