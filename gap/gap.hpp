#ifndef FLOW_RANGE_GAP_GAP_GAP_HPP
#define FLOW_RANGE_GAP_GAP_GAP_HPP

#include "kautil/algorithm/btree_search/btree_search.hpp"


template<typename preference_t>
struct gap_iterator;

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
    
        
        
        
    }
    
    uint64_t size(){ return pref->size() / kBlockSize; }
    
    gap_iterator<preference_t> iterator();
    
private:
    
    static constexpr int kBothOvfSame=2;
    static constexpr int kEitherOvfLeft=4;
    static constexpr int kEitherOvfRight=8;
    static constexpr offset_type kBlockSize =sizeof(value_type)*2;
    
    bool l_adj = false; // need to adjust left pole
    bool r_adj = false; // need to adjust right pole
    int ovf_state = 0; // 0 : nothing ovf, 2 : ovf both,same,  4 : ovf both,different, 8 : ovf either
    offset_type begin_ =0;
    offset_type end_ = 0;
    value_type from=0;
    value_type to=0;
    preference_t * pref=0;
    friend struct gap_iterator<preference_t>;
};

    


template<typename preference_t>
struct gap_iterator{
    
    using self_type = gap_iterator;
    using value_type = typename preference_t::value_type;
    using offset_type = typename preference_t::offset_type;

    gap_iterator(gap_iterator const& )=default;
    gap_iterator(gap<preference_t> * p):p(p){}
    
    struct current{ value_type l =0;value_type r =0; }; 
    self_type begin(){ 
        auto res = *this;
        res.cur = p->begin_;
        res.ovf_state=p->ovf_state;
        res.update_virtual_condition_begin();
        res.update_virtual_condition_end();
        return res;
    }
    
    self_type end(){ 
        auto res = *this; 
        res.cur = p->end_;
        res.ovf_state=p->ovf_state;
        return res;
    }
    
    
    bool operator<(self_type const& r ){ return cur < r.cur; }
    
    
    self_type & operator++(){ 
        ovf_state = 
                  // if(cond_velem_beg)
                  virtual_begin_f*(ovf_state^gap<preference_t>::kEitherOvfLeft) 
                +!virtual_begin_f*(
                  //else if(cond_velem_end)
                      virtual_end_f*(ovf_state^gap<preference_t>::kEitherOvfRight)
                    +!virtual_end_f*(ovf_state)
                );
        
        update_virtual_condition_end();
        cur += (!(virtual_end_f|virtual_begin_f)*gap<preference_t>::kBlockSize);
        update_virtual_condition_begin();
        return *this; 
    }
    
    self_type & operator--(){ 
        
        update_virtual_condition_begin();
        cur -= (!(virtual_end_f|virtual_begin_f)*gap<preference_t>::kBlockSize);
        if((cur + gap<preference_t>::kBlockSize) == p->begin_){
            ovf_state = p->ovf_state;
            update_virtual_condition_begin();
            update_virtual_condition_end();
            //*this = begin();
        }
        update_virtual_condition_end();
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
            {
                auto p = m->p;
                auto value_ptr = (value_type*) 0;
                
                pref->read_value(m->cur,&(value_ptr = &res.l));
                pref->read_value(m->cur+sizeof(value_type),&(value_ptr = &res.r));
                
                bool l_adj = p->l_adj&(m->cur == p->begin_);
                bool r_adj = p->r_adj&(m->cur+(gap<preference_t>::kBlockSize) >= p->end_);
                
                // may not be efficient. 
                res.l=
                          l_adj*p->from // 0) from is not contaied by existing range(it exists in vacant), so adjust pole value with from. 
                        +!l_adj*res.l;
                res.r=
                          r_adj*p->to   // 1) same as 0) but this should occure the last iteration.
                        +!r_adj*res.r;
            }
            return res;
        };

        {
            // if overflow then left or right pole is expressed with input(expressed virtually) 
            // else expressed with the values reading from region
            if(virtual_begin_f | virtual_end_f){ 
                return lmb_virtual_value(p->from,p->to,virtual_begin_f,p->pref);
            }else{ 
                return lmb_real_value(this,p->pref);
            }
        }
    }
    
    bool operator!=(self_type & r){ return (cur != r.cur) &(cur != r.cur+sizeof(value_type)); }
    
private:
    
    void update_virtual_condition_begin(){
        virtual_begin_f = (cur==p->begin_)*bool(ovf_state&gap<preference_t>::kEitherOvfLeft); // need not to be member
    }
    void update_virtual_condition_end(){
        virtual_end_f = ((cur+gap<preference_t>::kBlockSize)>=p->end_)*bool(ovf_state&gap<preference_t>::kEitherOvfRight); 
    }
    
//    void move_cur(offset_type value){
//        cur += value; 
//        update_virtual_condition_begin();
//        update_virtual_condition_end();
//    }

    
    bool virtual_end_f = false; // this member is compromise. i could not express the detection of last elem without this flag.  
    bool virtual_begin_f = false; 
    int ovf_state = 0; // 0 : nothing ovf, 2 : ovf both,same,  4 : ovf both,different, 8 : ovf either
    offset_type cur = 0;
    gap<preference_t> * p=0;
    
};


template<typename preference_t>
gap_iterator<preference_t> gap<preference_t>::iterator(){
    return gap_iterator<preference_t>(this);
}



#endif
