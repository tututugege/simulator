#include "Dcache.h"
#include <cstdio>
#include <cstring>
bool hit_ld = false;
bool hit_st = false;

int hit_way_ld = -1;
int hit_way_st = -1;

uint32_t hit_data_ld = 0;
uint32_t hit_data_st = 0;

bool stall_ld = false;
bool stall_st = false;

bool global_flush = false;
bool global_mispred1 = false;
bool global_mispred2 = false;
bool uncache_access = false;
void Dcache::init()
{
    hit_num = 0;
    miss_num = 0;

    memset(&s1_reg_ld, 0, sizeof(Pipe_Reg));
    memset(&s2_reg_ld, 0, sizeof(Pipe_Reg));
    memset(&s1_reg_st, 0, sizeof(Pipe_Reg));
    memset(&s2_reg_st, 0, sizeof(Pipe_Reg));
    memset(&s1_next_ld, 0, sizeof(Pipe_Reg));
    memset(&s2_next_ld, 0, sizeof(Pipe_Reg));
    memset(&s1_next_st, 0, sizeof(Pipe_Reg));
    memset(&s2_next_st, 0, sizeof(Pipe_Reg));
}
void Dcache::comb_out()
{
    out.dcache2ldq_resp->valid = hit_ld && s2_reg_ld.valid;
    out.dcache2ldq_resp->wen = false;
    out.dcache2ldq_resp->addr = s2_reg_ld.addr;
    out.dcache2ldq_resp->data = hit_data_ld;
    out.dcache2ldq_resp->uop = s2_reg_ld.uop;

    out.dcache2stq_resp->valid = hit_st && s2_reg_st.valid;
    out.dcache2stq_resp->wen = true;
    out.dcache2stq_resp->addr = s2_reg_st.addr;
    out.dcache2stq_resp->data = 0;
    out.dcache2stq_resp->uop = s2_reg_st.uop;

    out.dcache2mshr_ld->valid = !hit_ld && s2_reg_ld.valid && !stall_ld;
    out.dcache2mshr_ld->wen = false;
    out.dcache2mshr_ld->addr = s2_reg_ld.addr;
    out.dcache2mshr_ld->uop = s2_reg_ld.uop;

    out.dcache2mshr_st->valid = !hit_st && s2_reg_st.valid && !stall_st;
    out.dcache2mshr_st->wen = true;
    out.dcache2mshr_st->addr = s2_reg_st.addr;
    out.dcache2mshr_st->uop = s2_reg_st.uop;

    out.dcache2ldq_ready->ready = stall_ld? false:true;
    out.dcache2stq_ready->ready = stall_st? false:true;
}
void Dcache::comb_s2()
{
    stall_ld = in.wb_arbiter2dcache->stall_ld;
    stall_st = in.wb_arbiter2dcache->stall_st;
    global_flush = in.control->flush;
    global_mispred1 = (in.control->br_mask & (1 << s1_reg_ld.uop.tag)) && in.control->mispred;
    global_mispred2 = (in.control->br_mask & (1 << s2_reg_ld.uop.tag)) && in.control->mispred;

    uncache_access = (s2_reg_ld.uop.page_fault_load == true) | (s2_reg_ld.addr == 0x1fd0e000) | (s2_reg_ld.addr == 0x1fd0e004);
    bool st_ld_same = (s2_reg_st.valid && s2_reg_ld.valid && s2_reg_ld.index == s2_reg_st.index && s2_reg_ld.tag == s2_reg_st.tag);
    
    if (s2_reg_st.valid)
    {
        hit_st = false;
        hit_data_st = 0;
        hit_check(s2_reg_st.index, s2_reg_st.tag, hit_st, hit_way_st, hit_data_st, tag_reg_st, data_reg_st);
        if (!hit_st&&!stall_st)
        {
            hit_way_st = getlru(s2_reg_st.index);
        }
    }
    
    if (s2_reg_ld.valid)
    {
        if(uncache_access){
            hit_ld = true;
            hit_data_ld = s2_reg_ld.wdata;
        }
        else {
            hit_ld = false;
            hit_data_ld = 0;
            hit_check(s2_reg_ld.index, s2_reg_ld.tag, hit_ld, hit_way_ld, hit_data_ld, tag_reg_ld, data_reg_ld);
            if (!hit_ld && !stall_ld)
            {
                hit_way_ld = st_ld_same?hit_way_st:getlru(s2_reg_ld.index);
            }
        }
    }

}
void Dcache::comb_s1()
{

    tag_and_data_read(s1_reg_ld.index, GET_OFFSET(s1_reg_ld.addr), tag_reg_ld, data_reg_ld);
    tag_and_data_read(s1_reg_st.index, GET_OFFSET(s1_reg_st.addr), tag_reg_st, data_reg_st);

    if (stall_ld)
    {
        s2_next_ld = s2_reg_ld; 
        s1_next_ld = s1_reg_ld;
    }
    else if (global_flush)
    {
        s2_next_ld.valid = false;
        s1_next_ld.valid = false;
        memset(tag_reg_ld, 0, sizeof(uint32_t) * DCACHE_WAY_NUM);
        memset(data_reg_ld, 0, sizeof(uint32_t) * DCACHE_WAY_NUM);
    }
    else if (global_mispred1)
    {
        s1_next_ld.valid = false;
        memset(tag_reg_ld, 0, sizeof(uint32_t) * DCACHE_WAY_NUM);
        memset(data_reg_ld, 0, sizeof(uint32_t) * DCACHE_WAY_NUM);
    }
    else if(global_mispred2)
    {
        s2_next_ld.valid = false;
        memset(tag_reg_ld, 0, sizeof(uint32_t) * DCACHE_WAY_NUM);
        memset(data_reg_ld, 0, sizeof(uint32_t) * DCACHE_WAY_NUM);
    }
    else
    {
        s2_next_ld = s1_reg_ld;
        s1_next_ld.valid = in.ldq2dcache_req->en;
        s1_next_ld.addr = in.ldq2dcache_req->addr;
        s1_next_ld.index = GET_INDEX(s1_next_ld.addr);
        s1_next_ld.tag = GET_TAG(s1_next_ld.addr);
        s1_next_ld.wdata = in.ldq2dcache_req->wdata;
        s1_next_ld.wstrb = in.ldq2dcache_req->wstrb;
        s1_next_ld.uop = in.ldq2dcache_req->uop;

        memcpy(tag_reg_ld, tag_next_ld, sizeof(uint32_t) * DCACHE_WAY_NUM);
        memcpy(data_reg_ld, data_next_ld, sizeof(uint32_t) * DCACHE_WAY_NUM);
    }

    if (stall_st)
    {
        s2_next_st = s2_reg_st; 
        s1_next_st = s1_reg_st;
    }
    else
    {
        s2_next_st = s1_reg_st;
        s1_next_st.valid = in.stq2dcache_req->en;
        s1_next_st.addr = in.stq2dcache_req->addr;
        s1_next_st.index = GET_INDEX(s1_next_st.addr);
        s1_next_st.tag = GET_TAG(s1_next_st.addr); 
        s1_next_st.wdata = in.stq2dcache_req->wdata;
        s1_next_st.wstrb = in.stq2dcache_req->wstrb;
        s1_next_st.uop = in.stq2dcache_req->uop;

        memcpy(tag_reg_st, tag_next_st, sizeof(uint32_t) * DCACHE_WAY_NUM);
        memcpy(data_reg_st, data_next_st, sizeof(uint32_t) * DCACHE_WAY_NUM);
    }
}
void Dcache::seq()
{
    s2_reg_ld = s2_next_ld;
    s1_reg_ld = s1_next_ld;

    s2_reg_st = s2_next_st;
    s1_reg_st = s1_next_st;

    if(hit_st&&s2_reg_st.valid){
        uint32_t new_data = write_data_mask(data_reg_st[hit_way_st], s2_reg_st.wdata, s2_reg_st.wstrb);
        dcache_data[s2_reg_st.index][hit_way_st][GET_OFFSET(s2_reg_st.addr)] = new_data;
        dcache_dirty[s2_reg_st.index][hit_way_st] = true;
    }

    if(hit_ld&&s2_reg_ld.valid && !stall_ld){
        updatelru(s2_reg_ld.index, hit_way_ld);
        hit_num++;
    }
    if(!hit_ld && s2_reg_ld.valid && !stall_ld){
        dcache_valid[s2_reg_ld.index][hit_way_ld] = false;
        miss_num++;
    }

    if(hit_st&&s2_reg_st.valid && !stall_st){
        updatelru(s2_reg_st.index, hit_way_st);
        hit_num++;
    }
    if(!hit_st && s2_reg_st.valid && !stall_st){
        dcache_valid[s2_reg_st.index][hit_way_st] = false;
        miss_num++;
    }
}