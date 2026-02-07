#include "Dcache.h"
#include "WriteBuffer.h"
#include <cstdio>
#include <cstring>
bool hit_ld = false;
bool hit_st = false;

int hit_way_ld = -1;
int hit_way_st = -1;

uint32_t hit_data_ld = 0;
uint32_t hit_data_st = 0;

uint32_t new_data = 0;

bool stall_ld = false;
bool stall_st = false;

bool global_flush = false;
bool global_mispred1 = false;
bool global_mispred2 = false;
bool uncache_access = false;

bool mmu_read = false;
uint32_t mmu_addr = 0;

uint32_t old_debug_data = 0;
int debug_way = -1;

extern long long total_num;
extern long long miss_num;
void Dcache::init() {
  total_num = 0;
  miss_num = 0;

  s1_reg_ld = {};
  s2_reg_ld = {};
  s1_reg_st = {};
  s2_reg_st = {};
  s1_next_ld = {};
  s2_next_ld = {};
  s1_next_st = {};
  s2_next_st = {};
}
void Dcache::comb_out_ldq() {
  out.dcache2ldq_resp->valid =
      hit_ld && s2_reg_ld.valid && !global_mispred2 && !global_flush;
  out.dcache2ldq_resp->wen = false;
  out.dcache2ldq_resp->addr = s2_reg_ld.addr;
  out.dcache2ldq_resp->data = hit_data_ld;
  out.dcache2ldq_resp->uop = s2_reg_ld.uop;

  out.dcache2stq_resp->valid = hit_st && s2_reg_st.valid;
  out.dcache2stq_resp->wen = true;
  out.dcache2stq_resp->addr = s2_reg_st.addr;
  out.dcache2stq_resp->data = 0;
  out.dcache2stq_resp->uop = s2_reg_st.uop;
}
void Dcache::comb_out_mshr() {
  if (DCACHE_LOG) {
    printf("Dcache comb_out_mshr: hit_ld=%d hit_st=%d stall_ld=%d stall_st=%d "
           "global_mispred2=%d global_flush=%d\n",
           hit_ld, hit_st, stall_ld, stall_st, global_mispred2, global_flush);
  }
  out.dcache2mshr_ld->valid = !hit_ld && s2_reg_ld.valid && !stall_ld &&
                              !global_mispred2 && !global_flush;
  out.dcache2mshr_ld->wen = false;
  out.dcache2mshr_ld->addr = s2_reg_ld.addr;
  out.dcache2mshr_ld->uop = s2_reg_ld.uop;
  out.dcache2mshr_ld->wdata = 0;

  out.dcache2mshr_st->valid = !hit_st && s2_reg_st.valid && !stall_st;
  out.dcache2mshr_st->wen = true;
  out.dcache2mshr_st->wstrb = s2_reg_st.wstrb;
  out.dcache2mshr_st->addr = s2_reg_st.addr;
  out.dcache2mshr_st->uop = s2_reg_st.uop;
  out.dcache2mshr_st->wdata = s2_reg_st.wdata;
}
void Dcache::comb_out_ready() {
  out.dcache2ldq_ready->ready = stall_ld ? false : true;
  out.dcache2stq_ready->ready = stall_st ? false : true;
}
void Dcache::comb_s2() {

  global_flush = in.control->flush;
  global_mispred1 =
      (in.control->br_mask & (1ULL << s1_reg_ld.uop.tag)) && in.control->mispred;
  global_mispred2 =
      (in.control->br_mask & (1ULL << s2_reg_ld.uop.tag)) && in.control->mispred;

  uncache_access = (s2_reg_ld.uop.page_fault_load == true) |
                   (s2_reg_ld.addr == 0x1fd0e000) |
                   (s2_reg_ld.addr == 0x1fd0e004);

  if (s2_reg_st.valid) {
    hit_st = false;
    hit_data_st = 0;
    hit_check(s2_reg_st.index, s2_reg_st.tag, hit_st, hit_way_st, hit_data_st,
              tag_next_st, data_next_st);
    new_data = write_data_mask(data_next_st[hit_way_st], s2_reg_st.wdata,
                               s2_reg_st.wstrb);
  }

  if (s2_reg_ld.valid) {
    if (uncache_access) {
      hit_ld = true;
      hit_data_ld = s2_reg_ld.wdata;
    } else {
      hit_ld = false;
      hit_data_ld = 0;
      hit_check(s2_reg_ld.index, s2_reg_ld.tag, hit_ld, hit_way_ld, hit_data_ld,
                tag_next_ld, data_next_ld);
      if (hit_ld && in.mshr2dcache_fwd->valid &&
          in.mshr2dcache_fwd->addr == s2_reg_ld.addr) {
        hit_data_ld = in.mshr2dcache_fwd->rdata;
        if (DCACHE_LOG) {
          printf("Dcache Forward Data from MSHR: addr=0x%08X rdata=0x%08X "
                 "sim_time:%lld\n",
                 in.mshr2dcache_fwd->addr, in.mshr2dcache_fwd->rdata, sim_time);
        }
      }
    }
  }
  if (DCACHE_LOG) {
    printf("data_next_ld[0]=0x%08X data_next_ld[1]=0x%08X "
           "data_next_ld[2]=0x%08X data_next_ld[3]=0x%08X\n",
           data_next_ld[0], data_next_ld[1], data_next_ld[2], data_next_ld[3]);
    printf("data_next_st[0]=0x%08X data_next_st[1]=0x%08X "
           "data_next_st[2]=0x%08X data_next_st[3]=0x%08X\n",
           data_next_st[0], data_next_st[1], data_next_st[2], data_next_st[3]);
    printf("tag_next_ld[0]=0x%08X tag_next_ld[1]=0x%08X tag_next_ld[2]=0x%08X "
           "tag_next_ld[3]=0x%08X\n",
           tag_next_ld[0], tag_next_ld[1], tag_next_ld[2], tag_next_ld[3]);
    printf("tag_next_st[0]=0x%08X tag_next_st[1]=0x%08X tag_next_st[2]=0x%08X "
           "tag_next_st[3]=0x%08X\n",
           tag_next_st[0], tag_next_st[1], tag_next_st[2], tag_next_st[3]);
  }
  if (DEBUG) {
    if (in.stq2dcache_req->en &&
        ((in.stq2dcache_req->addr & 0xfffffffc) == DEBUG_ADDR)) {
      printf("Dcache Store Access Debug Addr in S1 Stage: valid=%d, "
             "uop_inst=0x%08x, rob_idx=%d addr=0x%08x, tag=0x%08x, "
             "index=0x%02X, wdata=0x%08x, wstrb=0x%X  sim_time:%lld\n",
             in.stq2dcache_req->en, in.stq2dcache_req->uop.instruction,
             in.stq2dcache_req->uop.rob_idx, in.stq2dcache_req->addr,
             GET_TAG(in.stq2dcache_req->addr),
             GET_INDEX(in.stq2dcache_req->addr), in.stq2dcache_req->wdata,
             in.stq2dcache_req->wstrb, sim_time);
    }
    if (s1_reg_st.valid && ((s1_reg_st.addr & 0xfffffffc) == DEBUG_ADDR)) {
      printf("Dcache Store Access Debug Addr in S2 Stage: valid=%d, "
             "uop_inst=0x%08x, rob_idx=%d addr=0x%08x, tag=0x%08x, "
             "index=0x%02X, wdata=0x%08x, wstrb=0x%08X  sim_time:%lld\n",
             s1_reg_st.valid, s1_reg_st.uop.instruction, s1_reg_st.uop.rob_idx,
             s1_reg_st.addr, s1_reg_st.tag, s1_reg_st.index, s1_reg_st.wdata,
             s1_reg_st.wstrb, sim_time);
    }
  }
}
void Dcache::comb_s1() {

  stall_ld = in.wb_arbiter2dcache->stall_ld |
             (in.mshr2dcache_ready->ready == false && !hit_ld &&
              s2_reg_ld.valid && !global_mispred2 && !global_flush);
  stall_st =
      in.wb_arbiter2dcache->stall_st |
      (in.mshr2dcache_ready->ready == false && !hit_st && s2_reg_st.valid);
  if (!stall_ld)
    tag_and_data_read(s1_reg_ld.index, GET_OFFSET(s1_reg_ld.addr), tag_reg_ld,
                      data_reg_ld);
  else
    tag_and_data_read(s2_reg_ld.index, GET_OFFSET(s2_reg_ld.addr), tag_reg_ld,
                      data_reg_ld);
  if (!stall_st)
    tag_and_data_read(s1_reg_st.index, GET_OFFSET(s1_reg_st.addr), tag_reg_st,
                      data_reg_st);
  else
    tag_and_data_read(s2_reg_st.index, GET_OFFSET(s2_reg_st.addr), tag_reg_st,
                      data_reg_st);
#ifdef CONFIG_MMU
  tag_and_data_read(GET_INDEX(in.ptw2dcache_req->paddr),
                    GET_OFFSET(in.ptw2dcache_req->paddr), mmu_reg_tag,
                    mmu_reg_data);
  if (DCACHE_LOG) {
    printf("Dcache MMU Read in S1 Stage: paddr=0x%08X\n",
           in.ptw2dcache_req->paddr);
    for (int i = 0; i < DCACHE_WAY_NUM; i++) {
      printf("  Way[%d]: Tag=0x%08X Data=0x%08X\n", i, mmu_reg_tag[i],
             mmu_reg_data[i]);
    }
  }
#endif

  if (stall_ld) {
    s2_next_ld = s2_reg_ld;
    s1_next_ld = s1_reg_ld;
    if (global_mispred1) {
      s1_next_ld.valid = false;
    }
    if (global_mispred2) {
      s2_next_ld.valid = false;
    }
    if (DCACHE_LOG) {
      printf("Dcache Stall Load in S1 Stage\n");
    }
  } else if (global_flush) {
    s2_next_ld.valid = false;
    s1_next_ld.valid = false;
    if (DCACHE_LOG) {
      printf("Dcache Global Flush in S1 Stage\n");
    }
  } else if (global_mispred1) {
    s2_next_ld.valid = false;
    s1_next_ld.valid = in.ldq2dcache_req->en;
    s1_next_ld.addr = in.ldq2dcache_req->addr;
    s1_next_ld.index = GET_INDEX(s1_next_ld.addr);
    s1_next_ld.tag = GET_TAG(s1_next_ld.addr);
    s1_next_ld.wdata = in.ldq2dcache_req->wdata;
    s1_next_ld.wstrb = in.ldq2dcache_req->wstrb;
    s1_next_ld.uop = in.ldq2dcache_req->uop;
    if (DCACHE_LOG) {
      printf("Dcache Global Mispred1 in S1 Stage\n");
    }
  } else {
    s2_next_ld = s1_reg_ld;
    s1_next_ld.valid = in.ldq2dcache_req->en;
    s1_next_ld.addr = in.ldq2dcache_req->addr;
    s1_next_ld.index = GET_INDEX(s1_next_ld.addr);
    s1_next_ld.tag = GET_TAG(s1_next_ld.addr);
    s1_next_ld.wdata = in.ldq2dcache_req->wdata;
    s1_next_ld.wstrb = in.ldq2dcache_req->wstrb;
    s1_next_ld.uop = in.ldq2dcache_req->uop;
    // if (DCACHE_LOG)
    // {
    //     printf("Dcache Load New Request in S1 Stage: valid=%d,
    //     uop_inst=0x%08x, rob_idx=%d addr=0x%08x, tag=0x%08x, index=0x%02X,
    //     wdata=0x%08x, wstrb=0x%X \n",
    //            s1_next_ld.valid, s1_next_ld.uop.instruction,
    //            s1_next_ld.uop.rob_idx, s1_next_ld.addr, s1_next_ld.tag,
    //            s1_next_ld.index, s1_next_ld.wdata, s1_next_ld.wstrb);
    //     printf("tag_reg_ld[0]=0x%08X tag_reg_ld[1]=0x%08X
    //     tag_reg_ld[2]=0x%08X tag_reg_ld[3]=0x%08X\n",
    //            tag_reg_ld[0], tag_reg_ld[1], tag_reg_ld[2], tag_reg_ld[3]);
    //     printf("data_reg_ld[0]=0x%08X data_reg_ld[1]=0x%08X
    //     data_reg_ld[2]=0x%08X data_reg_ld[3]=0x%08X\n",
    //            data_reg_ld[0], data_reg_ld[1], data_reg_ld[2],
    //            data_reg_ld[3]);
    // }
  }

  if (stall_st) {
    s2_next_st = s2_reg_st;
    s1_next_st = s1_reg_st;
    if (DCACHE_LOG) {
      printf("Dcache Stall Store in S1 Stage\n");
    }
  } else {
    s2_next_st = s1_reg_st;
    s1_next_st.valid = in.stq2dcache_req->en;
    s1_next_st.addr = in.stq2dcache_req->addr;
    s1_next_st.index = GET_INDEX(s1_next_st.addr);
    s1_next_st.tag = GET_TAG(s1_next_st.addr);
    s1_next_st.wdata = in.stq2dcache_req->wdata;
    s1_next_st.wstrb = in.stq2dcache_req->wstrb;
    s1_next_st.uop = in.stq2dcache_req->uop;

    if (DCACHE_LOG) {
      printf("Dcache Store New Request in S1 Stage: valid=%d, uop_inst=0x%08x, "
             "rob_idx=%d addr=0x%08x, tag=0x%08x, index=0x%02X, wdata=0x%08x, "
             "wstrb=0x%X \n",
             s1_next_st.valid, s1_next_st.uop.instruction,
             s1_next_st.uop.rob_idx, s1_next_st.addr, s1_next_st.tag,
             s1_next_st.index, s1_next_st.wdata, s1_next_st.wstrb);
    }
  }
}
void Dcache::seq() {
  if (DCACHE_LOG) {
    print();
  }
  if (DEBUG) {
    for (int i = 0; i < DCACHE_WAY_NUM; i++) {
      if (dcache_tag[GET_INDEX(DEBUG_ADDR)][i] == GET_TAG(DEBUG_ADDR) &&
          dcache_valid[GET_INDEX(DEBUG_ADDR)][i] &&
          old_debug_data !=
              dcache_data[GET_INDEX(DEBUG_ADDR)][i][GET_OFFSET(DEBUG_ADDR)]) {
        old_debug_data =
            dcache_data[GET_INDEX(DEBUG_ADDR)][i][GET_OFFSET(DEBUG_ADDR)];
        printf("\nDebug Dcache Read at addr:0x%08x data:0x%08x way:%d "
               "sim_time:%lld\n",
               DEBUG_ADDR, old_debug_data, i, sim_time);
        debug_way = i;
      }
    }
    if (debug_way != -1 &&
        dcache_tag[GET_INDEX(DEBUG_ADDR)][debug_way] != GET_TAG(DEBUG_ADDR)) {
      printf("\nDebug Dcache Read Miss at addr:0x%08x debug_way:%d "
             "dcache_tag:%d get_tag:%d sim_time:%lld\n",
             DEBUG_ADDR, debug_way,
             dcache_tag[GET_INDEX(DEBUG_ADDR)][debug_way], GET_TAG(DEBUG_ADDR),
             sim_time);
      debug_way = -1;
    }
  }

  if (hit_st && s2_reg_st.valid) {
    dcache_data[s2_reg_st.index][hit_way_st][GET_OFFSET(s2_reg_st.addr)] =
        new_data;
    dcache_dirty[s2_reg_st.index][hit_way_st] = true;
    if (s2_reg_st.tag == s1_reg_ld.tag && s2_reg_st.index == s1_reg_ld.index &&
        GET_OFFSET(s2_reg_st.addr) == GET_OFFSET(s1_reg_ld.addr) && !stall_ld) {
      data_reg_ld[hit_way_st] = new_data;
      if (DCACHE_LOG) {
        printf(
            "Dcache Store Forwarding in S1 Stage LD: way=%d new_data=0x%08X\n",
            hit_way_st, new_data);
      }
    }
    if (s2_reg_st.tag == s1_reg_st.tag && s2_reg_st.index == s1_reg_st.index &&
        GET_OFFSET(s2_reg_st.addr) == GET_OFFSET(s1_reg_st.addr) && !stall_st) {
      data_reg_st[hit_way_st] = new_data;
      if (DCACHE_LOG) {
        printf(
            "Dcache Store Forwarding in S1 Stage ST: way=%d new_data=0x%08X\n",
            hit_way_st, new_data);
      }
    }
  }

  if (hit_ld && s2_reg_ld.valid && !stall_ld) {
    updatelru(s2_reg_ld.index, hit_way_ld);
    total_num++;
  }
  if (!hit_ld && s2_reg_ld.valid && !stall_ld) {
    total_num++;
  }

  if (hit_st && s2_reg_st.valid && !stall_st) {
    updatelru(s2_reg_st.index, hit_way_st);
    total_num++;
  }
  if (!hit_st && s2_reg_st.valid && !stall_st) {
    total_num++;
  }
  s2_reg_ld = s2_next_ld;
  s1_reg_ld = s1_next_ld;

  s2_reg_st = s2_next_st;
  s1_reg_st = s1_next_st;
  memcpy(tag_next_ld, tag_reg_ld, sizeof(uint32_t) * DCACHE_WAY_NUM);
  memcpy(data_next_ld, data_reg_ld, sizeof(uint32_t) * DCACHE_WAY_NUM);
  memcpy(tag_next_st, tag_reg_st, sizeof(uint32_t) * DCACHE_WAY_NUM);
  memcpy(data_next_st, data_reg_st, sizeof(uint32_t) * DCACHE_WAY_NUM);

#ifdef CONFIG_MMU
  memcpy(mmu_next_tag, mmu_reg_tag, sizeof(uint32_t) * DCACHE_WAY_NUM);
  memcpy(mmu_next_data, mmu_reg_data, sizeof(uint32_t) * DCACHE_WAY_NUM);
  mmu_read = in.ptw2dcache_req->valid;
  mmu_addr = in.ptw2dcache_req->paddr;
#endif
}
#ifdef CONFIG_MMU
void Dcache::comb_mmu() {

  if (mmu_read && in.ptw2dcache_resp->ready) {
    bool hit = false;
    int way_idx = 0;
    uint32_t index = GET_INDEX(mmu_addr);
    uint32_t tag = GET_TAG(mmu_addr);
    uint32_t hit_data = 0;
    uint32_t rdata = 0;
    hit_check(index, tag, hit, way_idx, hit_data, mmu_next_tag, mmu_next_data);

    if (hit) {
      rdata = hit_data;
      updatelru(index, way_idx);
      if (DCACHE_LOG) {
        printf(
            "Dcache Read Hit: addr=0x%08X rdata=0x%08X way=%d sim_time:%lld\n",
            mmu_addr, rdata, way_idx, sim_time);
      }
    } else {
      hit = writebuffer_find(GET_ADDR(tag, index, 0), GET_OFFSET(mmu_addr),
                             rdata);
      if (hit) {
        if (DCACHE_LOG) {
          printf("Dcache Read Hit in WriteBuffer: addr=0x%08X rdata=0x%08X "
                 "sim_time:%lld\n",
                 mmu_addr, rdata, sim_time);
        }
      }
      if (DCACHE_LOG) {
        printf("Dcache Read Miss: addr=0x%08X sim_time:%lld\n", mmu_addr,
               sim_time);
      }
    }
    out.dcache2ptw_resp->valid = true;
    out.dcache2ptw_resp->miss = !hit;
    out.dcache2ptw_resp->data = rdata;
  } else {
    out.dcache2ptw_resp->valid = false;
  }
  out.dcache2ptw_req->ready = true;
  if (DCACHE_LOG) {
    printf("Dcache comb_mmu: mmu_read=%d in.ptw2dcache_resp->ready=%d\n",
           mmu_read, in.ptw2dcache_resp->ready);
    printf("in.ptw2dcache_req->paddr=0x%08X in.ptw2dcache_req->valid=%d\n",
           in.ptw2dcache_req->paddr, in.ptw2dcache_req->valid);
    printf("out.dcache2ptw_req->ready=%d\n", out.dcache2ptw_req->ready);
    printf("out.dcache2ptw_resp->valid=%d out.dcache2ptw_resp->miss=%d "
           "out.dcache2ptw_resp->data=0x%08X\n",
           out.dcache2ptw_resp->valid, out.dcache2ptw_resp->miss,
           out.dcache2ptw_resp->data);
    printf("mmu_next_tag[0]=0x%08X mmu_next_tag[1]=0x%08X "
           "mmu_next_tag[2]=0x%08X mmu_next_tag[3]=0x%08X\n",
           mmu_next_tag[0], mmu_next_tag[1], mmu_next_tag[2], mmu_next_tag[3]);
    printf("mmu_next_data[0]=0x%08X mmu_next_data[1]=0x%08X "
           "mmu_next_data[2]=0x%08X mmu_next_data[3]=0x%08X\n",
           mmu_next_data[0], mmu_next_data[1], mmu_next_data[2],
           mmu_next_data[3]);
    printf("out.dcache2ptw_resp->valid=%d out.dcache2ptw_resp->miss=%d "
           "out.dcache2ptw_resp->data=0x%08X sim_time:%lld\n",
           out.dcache2ptw_resp->valid, out.dcache2ptw_resp->miss,
           out.dcache2ptw_resp->data, sim_time);
  }
}
#endif
void Dcache::print() {
  printf("\n");

  uint32_t debug_index = GET_INDEX(DEBUG_ADDR);
  for (int i = 0; i < DCACHE_WAY_NUM; i++) {
    printf("Dcache Line[%02X] Way[%d]: Valid=%d Dirty=%d LRU=%d Tag=0x%08X "
           "Data[0]=0x%08X Data[1]=0x%08X Data[2]=0x%08X Data[3]=0x%08X \n",
           debug_index, i, dcache_valid[debug_index][i],
           dcache_dirty[debug_index][i], dcache_lru[debug_index][i],
           dcache_tag[debug_index][i], dcache_data[debug_index][i][0],
           dcache_data[debug_index][i][1], dcache_data[debug_index][i][2],
           dcache_data[debug_index][i][3]);
  }

  printf("\n");
  printf("Dcache State: hit_num=%lld, miss_num=%lld hit_ld:%d hit_st:%d "
         "stall_ld:%d stall_st:%d global_flush:%d global_mispred1:%d "
         "global_mispred2:%d\n",
         total_num - miss_num, miss_num, hit_ld, hit_st, stall_ld, stall_st,
         global_flush, global_mispred1, global_mispred2);
  printf("Dcache Load S1: valid=%d, uop_inst=0x%08x, rob_idx=%d addr=0x%08x, "
         "tag=0x%08x, index=0x%02X, wdata=0x%08x, wstrb=0x%X \n",
         s1_reg_ld.valid, s1_reg_ld.uop.instruction, s1_reg_ld.uop.rob_idx,
         s1_reg_ld.addr, s1_reg_ld.tag, s1_reg_ld.index, s1_reg_ld.wdata,
         s1_reg_ld.wstrb);
  printf("Dcache Load S2: valid=%d, uop_inst=0x%08x, rob_idx=%d addr=0x%08x, "
         "tag=0x%08x, index=0x%02X, wdata=0x%08x, wstrb=0x%X \n",
         s2_reg_ld.valid, s2_reg_ld.uop.instruction, s2_reg_ld.uop.rob_idx,
         s2_reg_ld.addr, s2_reg_ld.tag, s2_reg_ld.index, s2_reg_ld.wdata,
         s2_reg_ld.wstrb);
  printf("Dcache Store S1: valid=%d, uop_inst=0x%08x, rob_idx=%d addr=0x%08x, "
         "tag=0x%08x, index=0x%02X, wdata=0x%08x, wstrb=0x%X \n",
         s1_reg_st.valid, s1_reg_st.uop.instruction, s1_reg_st.uop.rob_idx,
         s1_reg_st.addr, s1_reg_st.tag, s1_reg_st.index, s1_reg_st.wdata,
         s1_reg_st.wstrb);
  printf("Dcache Store S2: valid=%d, uop_inst=0x%08x, rob_idx=%d addr=0x%08x, "
         "tag=0x%08x, index=0x%02X, wdata=0x%08x, wstrb=0x%X \n",
         s2_reg_st.valid, s2_reg_st.uop.instruction, s2_reg_st.uop.rob_idx,
         s2_reg_st.addr, s2_reg_st.tag, s2_reg_st.index, s2_reg_st.wdata,
         s2_reg_st.wstrb);

  printf("Dcache Input Requests:\n");
  printf("  Load Queue Request: en=%d, uop_inst=0x%08x, rob_idx=%d "
         "addr=0x%08x, wdata=0x%08x, wstrb=0x%X \n",
         in.ldq2dcache_req->en, in.ldq2dcache_req->uop.instruction,
         in.ldq2dcache_req->uop.rob_idx, in.ldq2dcache_req->addr,
         in.ldq2dcache_req->wdata, in.ldq2dcache_req->wstrb);
  printf("  Store Queue Request: en=%d, uop_inst=0x%08x, rob_idx=%d "
         "addr=0x%08x, wdata=0x%08x, wstrb=0x%X \n",
         in.stq2dcache_req->en, in.stq2dcache_req->uop.instruction,
         in.stq2dcache_req->uop.rob_idx, in.stq2dcache_req->addr,
         in.stq2dcache_req->wdata, in.stq2dcache_req->wstrb);

  printf("Dcache Output Responses:\n");
  printf("  Load Queue Response: valid=%d, uop_inst=0x%08x, rob_idx=%d wen=%d, "
         "addr=0x%08x, data=0x%08x\n",
         out.dcache2ldq_resp->valid, out.dcache2ldq_resp->uop.instruction,
         out.dcache2ldq_resp->uop.rob_idx, out.dcache2ldq_resp->wen,
         out.dcache2ldq_resp->addr, out.dcache2ldq_resp->data);
  printf("  Store Queue Response: valid=%d, uop_inst=0x%08x, rob_idx=%d "
         "wen=%d, addr=0x%08x, data=0x%08x\n",
         out.dcache2stq_resp->valid, out.dcache2stq_resp->uop.instruction,
         out.dcache2stq_resp->uop.rob_idx, out.dcache2stq_resp->wen,
         out.dcache2stq_resp->addr, out.dcache2stq_resp->data);

  printf("DCache Output MSHR Requests:\n");
  printf("  Load MSHR Request: valid=%d, wen=%d, addr=0x%08x, uop_inst=0x%08x, "
         "rob_idx=%d\n",
         out.dcache2mshr_ld->valid, out.dcache2mshr_ld->wen,
         out.dcache2mshr_ld->addr, out.dcache2mshr_ld->uop.instruction,
         out.dcache2mshr_ld->uop.rob_idx);
  printf("  Store MSHR Request: valid=%d, wen=%d, addr=0x%08x, "
         "uop_inst=0x%08x, rob_idx=%d\n",
         out.dcache2mshr_st->valid, out.dcache2mshr_st->wen,
         out.dcache2mshr_st->addr, out.dcache2mshr_st->uop.instruction,
         out.dcache2mshr_st->uop.rob_idx);
  printf("Dcache Output Ready: \n");
  printf("  Load Queue Ready: ready=%d\n", out.dcache2ldq_ready->ready);
  printf("  Store Queue Ready: ready=%d\n", out.dcache2stq_ready->ready);
}
