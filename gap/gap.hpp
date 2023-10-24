#ifndef FLOW_RANGE_GAP_GAP_GAP_HPP
#define FLOW_RANGE_GAP_GAP_GAP_HPP

#include "kautil/algorithm/btree_search/btree_search.hpp"

template<typename preference_t>
struct gap{

    using self_type = gap;
    using value_type = typename preference_t::value_type;
    using offset_type = typename preference_t::offset_type;

    gap(preference_t * pref) : pref(pref){}
    ~gap(){}

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
                  is_ovf_both_same*kBothOvfSame
                 +is_ovf_both_different*(kEitherOvfLeft|kEitherOvfRight)
                 +(is_ovf_either*b0.overflow)*kEitherOvfLeft
                 +(is_ovf_either*b1.overflow)*kEitherOvfRight;
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
        virtual_end_f=false;
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
        auto cond_velem_beg = (cur==begin_)*ovf_state&kEitherOvfLeft;
        auto cond_velem_end = ((cur+sizeof(value_type)*2)>=end_)*ovf_state&kEitherOvfRight;
        auto cond_velem = cond_velem_beg | cond_velem_end;
        
        ovf_state = 
                  cond_velem_beg*(ovf_state^kEitherOvfLeft)
                +!cond_velem_beg*ovf_state;
        
        ovf_state = 
                  cond_velem_end*(ovf_state^kEitherOvfRight)
                +!cond_velem_end*ovf_state;
        
        cur += (!cond_velem*kBlockSize);
        return *this; 
    }
    current operator*() { 
        
        auto lmb_virtual_value = [](auto from,auto to,bool lr, auto * pref)->current{
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
            }
            return cur;
        };
        
        auto lmb_real_value = [](auto * m,auto * pref) -> current{
            auto res=current{};
            auto value_ptr = (value_type*) 0;
            
            
            pref->read_value(m->cur,&(value_ptr = &res.l));
            pref->read_value(m->cur+sizeof(value_type),&(value_ptr = &res.r));
            auto adjust_r = (m->r_adj&(m->cur+(kBlockSize) >= m->end_)); // add condition to detect last one
            
            // may not be efficient. 
            res.l=
                      m->l_adj*m->from // 0) from is not contaied by existing range(it exists in vacant), so adjust pole value with from. 
                    +!m->l_adj*res.l;
            res.r=
                      adjust_r*m->to   // 1) same as 0) but this should occure the last iteration.
                    +!adjust_r*res.r;
            m->l_adj=false; // only first time is concerned if it is true
            return res;
        };

        {
            auto virtual_begin = (cur==begin_)*ovf_state&kEitherOvfLeft; 
            if(virtual_begin | virtual_end_f){
                virtual_end_f=false;
                return lmb_virtual_value(from,to,virtual_begin,pref);
            }else{
                virtual_end_f = ((cur+kBlockSize)>=end_)*ovf_state&kEitherOvfRight; 
                return lmb_real_value(this,pref);
            }
        }
    }
    
    bool operator!=(self_type & r){ return (cur != r.cur) &(cur != r.cur+sizeof(value_type)); }
    
private:
    
    static constexpr int kBothOvfSame=2;
    static constexpr int kEitherOvfLeft=4;
    static constexpr int kEitherOvfRight=8;
    static constexpr offset_type kBlockSize =sizeof(value_type)*2;
    
    bool virtual_end_f = false; // this member is compromise. i could not express the detection of last elem without this flag.  
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






#endif
