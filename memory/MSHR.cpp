#include "MSHR.h"
#include "WriteBuffer.h"
#include <cstdio>

mshr_entry mshr_entries[MSHR_ENTRY_SIZE];
table_entry mshr_table[MSHR_TABLE_SIZE];
enum MSHR_STATE mshr_state = MSHR_IDLE;

uint32_t mshr_way;
bool dirty_writeback = false;
uint32_t mshr_paddr;
bool ld_merge = false;
bool st_merge = false;
uint32_t trans_offset = 0;
InstUop trans_uop;
bool merge_flag_ld = false;
bool merge_flag_st = false;

extern long long total_num;
extern long long miss_num;

int fwd_index;
void MSHR::comb_out()
{
    out.mshr2arbiter->prority = count_mshr > 0 ? true : false;
    if (dirty_writeback && mshr_state == MSHR_WRITEBACK)
    {
        out.mshr2writebuffer->valid = true;
        out.mshr2writebuffer->addr = mshr_paddr;
        for (int i = 0; i < DCACHE_OFFSET_NUM; i++)
        {
            out.mshr2writebuffer->data[i] = dcache_data[mshr_entries[mshr_head].index][mshr_way][i];
        }
    }
    else
    {
        out.mshr2writebuffer->valid = false;
        out.mshr2writebuffer->addr = 0;
    }
    if (mshr_state == MSHR_DEAL)
    {
        out.mshr2arbiter_control->en = true;
        out.mshr2arbiter_control->wen = false;
        out.mshr2arbiter_control->addr = GET_ADDR(mshr_entries[mshr_head].tag, mshr_entries[mshr_head].index, 0);
        out.mshr2arbiter_control->wdata = 0;
        out.mshr2arbiter_control->len = DCACHE_OFFSET_NUM - 1;
        out.mshr2arbiter_control->size = 0b10;
        out.mshr2arbiter_control->sel = 0b1111;
        out.mshr2arbiter_control->done = false;
        out.mshr2arbiter_control->last = false;
    }
    else
    {
        out.mshr2arbiter_control->en = false;
        out.mshr2arbiter_control->wen = false;
        out.mshr2arbiter_control->addr = 0;
        out.mshr2arbiter_control->wdata = 0;
        out.mshr2arbiter_control->len = 0;
        out.mshr2arbiter_control->size = 0;
        out.mshr2arbiter_control->sel = 0;
        out.mshr2arbiter_control->done = false;
        out.mshr2arbiter_control->last = false;
    }

    if (mshr_state == MSHR_DEAL)
    {
        out.mshr2dcache_fwd->valid = in.arbiter2mshr_data->last;
        out.mshr2dcache_fwd->addr = GET_ADDR(mshr_entries[mshr_head].tag, mshr_entries[mshr_head].index, count_data - 1);
        out.mshr2dcache_fwd->rdata = in.arbiter2mshr_data->data;
    }
    else
    {
        out.mshr2dcache_fwd->valid = false;
        out.mshr2dcache_fwd->addr = 0;
        out.mshr2dcache_fwd->rdata = 0;
    }

    if (mshr_state == MSHR_TRAN)
    {
        if (done_type == 1)
        {
            out.mshr2cpu_resp->valid = true;
            out.mshr2cpu_resp->wen = false;
            out.mshr2cpu_resp->addr = GET_ADDR(mshr_entries[mshr_head].tag, mshr_entries[mshr_head].index, trans_offset);
            out.mshr2cpu_resp->data = dcache_data[mshr_entries[mshr_head].index][mshr_way][trans_offset];
            out.mshr2cpu_resp->uop = trans_uop;
        }
        else if (done_type == 2)
        {
            out.mshr2cpu_resp->valid = true;
            out.mshr2cpu_resp->wen = true;
            out.mshr2cpu_resp->addr = GET_ADDR(mshr_entries[mshr_head].tag, mshr_entries[mshr_head].index, trans_offset);
            out.mshr2cpu_resp->data = 0;
            out.mshr2cpu_resp->uop = trans_uop;
        }
        else
        {
            out.mshr2cpu_resp->valid = false;
            out.mshr2cpu_resp->addr = 0;
            out.mshr2cpu_resp->data = 0;
        }
    }
    else
    {
        out.mshr2cpu_resp->valid = false;
        out.mshr2cpu_resp->addr = 0;
        out.mshr2cpu_resp->data = 0;
    }
    if (DCACHE_LOG)
    {
        printf("mshr_state=%u done_type=%u, deal_index=%u\n", mshr_state, done_type, deal_index);
    }
}
void MSHR::comb_ready()
{
    out.mshr2dcache_ready->ready = (count_mshr < MSHR_ENTRY_SIZE - 1) && (count_table < MSHR_TABLE_SIZE - 1);
}
void MSHR::comb()
{
    ld_merge = in.dcache2mshr_ld->valid && GET_INDEX(in.dcache2mshr_ld->addr) == mshr_entries[mshr_head].index && GET_TAG(in.dcache2mshr_ld->addr) == mshr_entries[mshr_head].tag;
    st_merge = in.dcache2mshr_st->valid && GET_INDEX(in.dcache2mshr_st->addr) == mshr_entries[mshr_head].index && GET_TAG(in.dcache2mshr_st->addr) == mshr_entries[mshr_head].tag;
}
void MSHR::seq()
{
    if (DCACHE_LOG)
    {
        print();
    }
    if (in.control->mispred)
    {
        for (uint32_t i = 0; i < count_table; i++)
        {
            uint32_t idx = (table_head + i) % MSHR_TABLE_SIZE;
            if (mshr_table[idx].valid && mshr_table[idx].type == 0)
            {
                if (in.control->br_mask & (1ULL << mshr_table[idx].uop.tag))
                {
                    table_free(idx);
                }
            }
        }
    }
    if (in.control->flush)
    {
        for (uint32_t i = 0; i < count_table; i++)
        {
            uint32_t idx = (table_head + i) % MSHR_TABLE_SIZE;
            if (mshr_table[idx].valid && mshr_table[idx].type == 0)
            {
                table_free(idx);
            }
        }
    }
    if (mshr_state == MSHR_IDLE)
    {
        if (count_mshr > 0)
        {
            if (mshr_entries[mshr_head].valid)
            {
                if (mshr_entries[mshr_head].count > 0)
                {
                    mshr_entries[mshr_head].issued = true;
                    mshr_way = getlru(mshr_entries[mshr_head].index);
                    updatelru(mshr_entries[mshr_head].index, mshr_way);
                    mshr_paddr = GET_ADDR(dcache_tag[mshr_entries[mshr_head].index][mshr_way], mshr_entries[mshr_head].index, 0);
                    dcache_tag[mshr_entries[mshr_head].index][mshr_way] = mshr_entries[mshr_head].tag;
                    dcache_valid[mshr_entries[mshr_head].index][mshr_way] = false;
                    dirty_writeback = dcache_dirty[mshr_entries[mshr_head].index][mshr_way];
                    dcache_dirty[mshr_entries[mshr_head].index][mshr_way] = false;
                    fwd_index = find_in_writebuffer(GET_ADDR(mshr_entries[mshr_head].tag, mshr_entries[mshr_head].index, 0));
                    if (dirty_writeback)
                    {
                        mshr_state = MSHR_WRITEBACK;
                    }
                    else if (fwd_index != -1)
                    {

                        mshr_state = MSHR_FORWARD;
                    }
                    else
                    {
                        mshr_state = MSHR_DEAL;
                    }
                }
                else
                {
                    mshr_entries[mshr_head].valid = false;
                    mshr_head = (mshr_head + 1) % MSHR_ENTRY_SIZE;
                    count_mshr--;
                }
            }
        }
    }
    else if (mshr_state == MSHR_DEAL)
    {
        if (in.arbiter2mshr_data->done)
        {
            dcache_data[mshr_entries[mshr_head].index][mshr_way][count_data++] = in.arbiter2mshr_data->data;
        }
        if (in.arbiter2mshr_data->last)
        {
            count_data = 0;
            mshr_state = MSHR_TRAN;
        }
    }
    else if (mshr_state == MSHR_TRAN)
    {
        deal_index = MSHR_TABLE_SIZE;
        done_type = 0;
        merge_flag_ld = false;
        merge_flag_st = false;
        for (uint32_t i = 0; i < count_table; i++)
        {
            uint32_t table_index = (table_head + i) % MSHR_TABLE_SIZE;
            if (mshr_table[table_index].valid && mshr_table[table_index].entry == mshr_head)
            {
                deal_index = table_index;
                break;
            }
        }
        if (DCACHE_LOG)
        {
            if (deal_index != MSHR_TABLE_SIZE)
                printf("MSHR Deal Entry Found: index=%u type=%u offset=%u\n", deal_index, mshr_table[deal_index].type, mshr_table[deal_index].offset);
            else
                printf("MSHR Deal No Entry Found\n");
        }
        if (deal_index == MSHR_TABLE_SIZE)
        {
            if (st_merge)
            {
                done_type = 2;
                merge_flag_st = true;
                trans_offset = GET_OFFSET(in.dcache2mshr_st->addr);
                trans_uop = in.dcache2mshr_st->uop;
                dcache_data[mshr_entries[mshr_head].index][mshr_way][trans_offset] = write_data_mask(dcache_data[mshr_entries[mshr_head].index][mshr_way][trans_offset], in.dcache2mshr_st->wdata, in.dcache2mshr_st->wstrb);
                dcache_dirty[mshr_entries[mshr_head].index][mshr_way] = true;
                if (DCACHE_LOG)
                {
                    printf("\nMSHR Store Done: uop_inst=0x%08x rob_idx=%d Addr=0x%08x Wdata=0x%08x Wstrb=0x%08X \n", trans_uop.instruction, trans_uop.rob_idx, GET_ADDR(mshr_entries[mshr_head].tag, mshr_entries[mshr_head].index, trans_offset), in.arbiter2mshr_data->data, mshr_table[deal_index].wstrb);
                }
            }
            else if (ld_merge)
            {
                done_type = 1;
                trans_offset = GET_OFFSET(in.dcache2mshr_ld->addr);
                trans_uop = in.dcache2mshr_ld->uop;
                merge_flag_ld = true;
                if (DCACHE_LOG)
                {
                    printf("\nMSHR Load Done: uop_inst=0x%08x rob_idx=%d Addr=0x%08x Data=0x%08x \n", trans_uop.instruction, trans_uop.rob_idx, GET_ADDR(mshr_entries[mshr_head].tag, mshr_entries[mshr_head].index, trans_offset), dcache_data[mshr_entries[mshr_head].index][mshr_way][trans_offset]);
                }
            }
            else
            {
                if (mshr_entries[mshr_head].count)
                {
                    printf("MSHR Warning: Next MSHR entry not free when finishing current MSHR entry! %lld\n", sim_time);
                    exit(1);
                }
                count_mshr--;
                dcache_valid[mshr_entries[mshr_head].index][mshr_way] = true;
                mshr_entries[mshr_head].valid = false;
                mshr_head = (mshr_head + 1) % MSHR_ENTRY_SIZE;
                mshr_state = MSHR_IDLE;
                miss_num++;
                if (DCACHE_LOG)
                {
                    printf("MSHR Entry Finished: move to next MSHR entry %d\n", done_type);
                }
            }
        }
        else
        {
            trans_offset = mshr_table[deal_index].offset;
            trans_uop = mshr_table[deal_index].uop;
            if (mshr_table[deal_index].type == 0)
            {
                done_type = 1;
                if (DCACHE_LOG)
                {
                    printf("\nMSHR Load Done: uop_inst=0x%08x rob_idx=%d Addr=0x%08x Data=0x%08x \n", trans_uop.instruction, trans_uop.rob_idx, GET_ADDR(mshr_entries[mshr_head].tag, mshr_entries[mshr_head].index, trans_offset), dcache_data[mshr_entries[mshr_head].index][mshr_way][trans_offset]);
                }
                table_free(deal_index);
            }
            else
            {
                done_type = 2;
                dcache_data[mshr_entries[mshr_head].index][mshr_way][trans_offset] = write_data_mask(dcache_data[mshr_entries[mshr_head].index][mshr_way][trans_offset], mshr_table[deal_index].wdata, mshr_table[deal_index].wstrb);
                dcache_dirty[mshr_entries[mshr_head].index][mshr_way] = true;
                if (DCACHE_LOG)
                {
                    printf("\nMSHR Store Done: uop_inst=0x%08x rob_idx=%d Addr=0x%08x Wdata=0x%08x Wstrb=0x%08X \n", trans_uop.instruction, trans_uop.rob_idx, GET_ADDR(mshr_entries[mshr_head].tag, mshr_entries[mshr_head].index, trans_offset), in.arbiter2mshr_data->data, mshr_table[deal_index].wstrb);
                }
                table_free(deal_index);
            }
        }
    }
    else if (mshr_state == MSHR_WRITEBACK)
    {
        if (in.writebuffer2mshr->ready)
        {
            fwd_index = find_in_writebuffer(GET_ADDR(mshr_entries[mshr_head].tag, mshr_entries[mshr_head].index, 0));

            if (fwd_index != -1)
            {
                mshr_state = MSHR_FORWARD;
            }
            else
            {
                dirty_writeback = false;
                mshr_state = MSHR_DEAL;
            }
        }
    }
    else if (mshr_state == MSHR_FORWARD)
    {
        fwd(fwd_index, dcache_data[mshr_entries[mshr_head].index][mshr_way]);
        mshr_state = MSHR_TRAN;
    }
    if (mshr_table[table_head].valid == false && count_table > 0)
    {
        table_head = (table_head + 1) % MSHR_TABLE_SIZE;
        count_table--;
    }

    bool mispred = in.control->mispred && (in.control->br_mask & (1ULL << in.dcache2mshr_ld->uop.tag));
    if (in.dcache2mshr_ld->valid && !mispred && !merge_flag_ld)
    {
        uint32_t entry_idx = find_entry(in.dcache2mshr_ld->addr);
        if (entry_idx == MSHR_ENTRY_SIZE)
        {
            entry_idx = mshr_tail;
            entry_add(mshr_tail, GET_INDEX(in.dcache2mshr_ld->addr), GET_TAG(in.dcache2mshr_ld->addr));
        }
        else
        {
            mshr_entries[entry_idx].count++;
        }
        table_add(entry_idx, false, GET_OFFSET(in.dcache2mshr_ld->addr), 0, 0, 0, in.dcache2mshr_ld->uop);
    }
    if (in.dcache2mshr_st->valid && !merge_flag_st)
    {
        uint32_t entry_idx = find_entry(in.dcache2mshr_st->addr);
        if (entry_idx == MSHR_ENTRY_SIZE)
        {
            entry_idx = mshr_tail;
            entry_add(mshr_tail, GET_INDEX(in.dcache2mshr_st->addr), GET_TAG(in.dcache2mshr_st->addr));
        }
        else
        {
            mshr_entries[entry_idx].count++;
        }
        table_add(entry_idx, true, GET_OFFSET(in.dcache2mshr_st->addr), 0, in.dcache2mshr_st->wstrb, in.dcache2mshr_st->wdata, in.dcache2mshr_st->uop);
    }
}
void MSHR::table_free(uint32_t idx)
{
    mshr_table[idx].valid = false;
    mshr_entries[mshr_table[idx].entry].count--;
}
uint32_t MSHR::find_entry(uint32_t addr)
{
    uint32_t index = GET_INDEX(addr);
    uint32_t tag = GET_TAG(addr);
    for (int i = 0; i < MSHR_ENTRY_SIZE; i++)
    {
        if (mshr_entries[i].valid && mshr_entries[i].index == index && mshr_entries[i].tag == tag)
        {
            return i;
        }
    }
    return MSHR_ENTRY_SIZE;
}
void MSHR::table_add(uint32_t idx, bool type, uint32_t offset, uint32_t reg, uint32_t wstrb, uint32_t wdata, InstUop uop)
{
    mshr_table[table_tail].valid = true;
    mshr_table[table_tail].entry = idx;
    mshr_table[table_tail].type = type;
    mshr_table[table_tail].offset = offset;
    mshr_table[table_tail].reg = reg;
    mshr_table[table_tail].wstrb = wstrb;
    mshr_table[table_tail].wdata = wdata;
    mshr_table[table_tail].uop = uop;
    if (DCACHE_LOG)
    {
        printf("\nMSHR Table Add: table_tail=%u, rob_idx=%d, type=%d, offset=%u, reg=0x%08x, wstrb=0x%X, uop_inst=0x%08x\n", table_tail, uop.rob_idx, type, offset, reg, wstrb, uop.instruction);
    }
    table_tail = (table_tail + 1) % MSHR_TABLE_SIZE;
    count_table++;
    if (count_table > MSHR_TABLE_SIZE)
    {
        printf("MSHR Warning: MSHR table overflow! %lld\n", sim_time);
        exit(1);
    }
}
void MSHR::entry_add(uint32_t idx, uint32_t index, uint32_t tag)
{
    mshr_entries[mshr_tail].valid = true;
    mshr_entries[mshr_tail].index = index;
    mshr_entries[mshr_tail].tag = tag;
    mshr_entries[mshr_tail].count = 1;
    mshr_entries[mshr_tail].issued = false;
    if (DCACHE_LOG)
    {
        printf("\nMSHR Entry Add: idx=%u, index=0x%02X, tag=0x%08x\n", mshr_tail, index, tag);
    }
    mshr_tail = (mshr_tail + 1) % MSHR_ENTRY_SIZE;
    count_mshr++;
    if (count_mshr > MSHR_ENTRY_SIZE)
    {
        printf("MSHR Warning: MSHR entry overflow! %lld\n", sim_time);
        exit(1);
    }
}
void MSHR::init()
{
    mshr_head = 0;
    mshr_tail = 0;
    table_head = 0;
    table_tail = 0;
    count_mshr = 0;
    count_table = 0;
    count_data = 0;
    for (int i = 0; i < MSHR_ENTRY_SIZE; i++)
    {
        mshr_entries[i].valid = false;
        mshr_entries[i].issued = false;
        mshr_entries[i].count = 0;
    }
    for (int i = 0; i < MSHR_TABLE_SIZE; i++)
    {
        mshr_table[i].valid = false;
    }
    mshr_state = MSHR_IDLE;
}
void MSHR::print()
{
    printf("\n");

    printf("MSHR_INPUT:\n");
    // printf("in.dcache2mshr_ld->valid=%u addr=0x%08x uop_inst=0x%08x rob_idx=%u\n",
    //        in.dcache2mshr_ld->valid,
    //        in.dcache2mshr_ld->addr,
    //        in.dcache2mshr_ld->uop.instruction,
    //        in.dcache2mshr_ld->uop.rob_idx);
    // printf("in.dcache2mshr_st->valid=%u addr=0x%08x uop_inst=0x%08x rob_idx=%u wstrb=0x%X\n",
    //        in.dcache2mshr_st->valid,
    //        in.dcache2mshr_st->addr,
    //        in.dcache2mshr_st->uop.instruction,
    //        in.dcache2mshr_st->uop.rob_idx,
    //        in.dcache2mshr_st->wstrb);
    printf("MSHR Control State: flush:%d mispred:%d mshr_way:%d\n", in.control->flush, in.control->mispred, mshr_way);
    printf("MSHR Merge Info: ld_merge=%d, st_merge=%d merge_flag_ld:%d merge_flag_st:%d\n", ld_merge, st_merge, merge_flag_ld, merge_flag_st);
    printf("MSHR State: mshr_state:%d mshr_head=%u, mshr_tail=%u, table_head=%u, table_tail=%u, count_mshr=%u, count_table=%u\n", mshr_state, mshr_head, mshr_tail, table_head, table_tail, count_mshr, count_table);
    printf("MSHR Data Count: count_data=%u done_type=%u deal_index=%u\n", count_data, done_type, deal_index);
    printf("MSHR Outputs:\n");
    printf(" mshr2dcache_ready: ready=%d\n", out.mshr2dcache_ready->ready);
    printf(" mshr2arbiter_control: en=%d wen=%d addr=0x%08x wdata=0x%08x len=%u size=0x%X sel=0x%X done=%d last=%d\n", out.mshr2arbiter_control->en, out.mshr2arbiter_control->wen, out.mshr2arbiter_control->addr, out.mshr2arbiter_control->wdata, out.mshr2arbiter_control->len, out.mshr2arbiter_control->size, out.mshr2arbiter_control->sel, out.mshr2arbiter_control->done, out.mshr2arbiter_control->last);
    printf(" mshr2cpu_resp: valid=%d wen=%d addr=0x%08x data=0x%08x uop_inst=0x%08x rob_idx=%d\n", out.mshr2cpu_resp->valid, out.mshr2cpu_resp->wen, out.mshr2cpu_resp->addr, out.mshr2cpu_resp->data, out.mshr2cpu_resp->uop.instruction, out.mshr2cpu_resp->uop.rob_idx);
    printf(" mshr2writebuffer: valid=%d addr=0x%08x\n", out.mshr2writebuffer->valid, out.mshr2writebuffer->addr);
    printf("MSHR Entries:\n");
    for (int i = 0; i < MSHR_ENTRY_SIZE; i++)
    {
        printf(" Entry %d: valid=%d, index=0x%02X, tag=0x%08x, count=%u, issued=%d\n", i, mshr_entries[i].valid, mshr_entries[i].index, mshr_entries[i].tag, mshr_entries[i].count, mshr_entries[i].issued);
    }
    printf("MSHR Table:\n");
    for (int i = 0; i < MSHR_TABLE_SIZE; i++)
    {
        printf(" Table %d: valid=%d, inst=0x%08x rob_idx=%d entry=%u, type=%d, offset=%u, reg=0x%08x, wstrb=0x%X, wdata=0x%08x\n", i, mshr_table[i].valid, mshr_table[i].uop.instruction, mshr_table[i].uop.rob_idx, mshr_table[i].entry, mshr_table[i].type, mshr_table[i].offset, mshr_table[i].reg, mshr_table[i].wstrb, mshr_table[i].wdata);
    }
}
