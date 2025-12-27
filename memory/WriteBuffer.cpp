#include "WriteBuffer.h"
#include <cstdio>

WriteBuffer_entry write_buffer[WRITE_BUFFER_SIZE];

void WriteBuffer::init()
{
    head = 0;
    tail = 0;
    count = 0;
}
void WriteBuffer::comb_ready()
{
    out.writebuffer2mshr->ready = (count < WRITE_BUFFER_SIZE);
}
void WriteBuffer::comb_writemark()
{
    if (count >= HIGH_WATERMARK)
    {
        out.writebuffer2arbiter->arbiter_priority = true;
    }
    else if (count <= LOW_WATERMARK)
    {
        out.writebuffer2arbiter->arbiter_priority = false;
    }
}
void WriteBuffer::comb()
{
    if (write_buffer[head].valid)
    {
        out.writebuffer2arbiter_control->en = true;
        out.writebuffer2arbiter_control->wen = true;
        out.writebuffer2arbiter_control->wdata = write_buffer[head].data[wdone_count]; // Simplified for demo
        out.writebuffer2arbiter_control->addr = write_buffer[head].addr;
        out.writebuffer2arbiter_control->len = DCACHE_OFFSET_NUM - 1;
        out.writebuffer2arbiter_control->size = 0b10;
        out.writebuffer2arbiter_control->sel = 0b1111;
        out.writebuffer2arbiter_control->done = wdone & !in.arbiter2writebuffer_data->done;
        out.writebuffer2arbiter_control->last = (wdone_count == DCACHE_OFFSET_NUM - 1) & wdone;
    }else{
        out.writebuffer2arbiter_control->en = false;
        out.writebuffer2arbiter_control->wen = false;
        out.writebuffer2arbiter_control->wdata = 0;
        out.writebuffer2arbiter_control->addr = 0;
        out.writebuffer2arbiter_control->len = 0;
        out.writebuffer2arbiter_control->size = 0;
        out.writebuffer2arbiter_control->sel = 0;
        out.writebuffer2arbiter_control->done = false;
        out.writebuffer2arbiter_control->last = false;
    }
}
void WriteBuffer::seq()
{
    if(DCACHE_LOG){
        print();
    }
    if (write_buffer[head].valid)
    {
        if (wdone_count == 0)
        {
            wdone = true;
        }
        else if (in.arbiter2writebuffer_data->done)
        {
            wdone = false;
        }
        else
        {
            wdone = true;
        }

        if (in.arbiter2writebuffer_data->done)
        {
            wdone_count++;
        }
        if (in.arbiter2writebuffer_data->last)
        {
            write_buffer[head].valid = 0;
            wdone = false;
            wdone_count = 0;
            if (count > 0)
            {
                head = (head + 1) % WRITE_BUFFER_SIZE;
                count--;
            }
        }
    }
    if(in.mshr2writebuffer->valid&&(count<WRITE_BUFFER_SIZE))
    {
        write_buffer[tail].valid = true;
        write_buffer[tail].addr = in.mshr2writebuffer->addr;
        for(int i=0;i<DCACHE_OFFSET_NUM;i++)
        {
            write_buffer[tail].data[i] = in.mshr2writebuffer->data[i];
        }
        tail = (tail + 1) % WRITE_BUFFER_SIZE;
        count++;
        if(count>WRITE_BUFFER_SIZE){
            printf("WriteBuffer Warning: WriteBuffer overflow! %lld\n",sim_time);
            exit(1);
        }
    }
}
void WriteBuffer::print()
{
    printf("\nWriteBuffer State: head=%u, tail=%u, count=%u wdone=%d wdone_count=%d\n", head, tail, count, wdone, wdone_count);
    for (int i = 0; i < WRITE_BUFFER_SIZE; i++)
    {
        printf("Entry %d: valid=%d, addr=0x%08x, data=[", i, write_buffer[i].valid, write_buffer[i].addr);
        for (int j = 0; j < DCACHE_OFFSET_NUM; j++)
        {
            printf("0x%08x", write_buffer[i].data[j]);
            if (j != DCACHE_OFFSET_NUM - 1)
                printf(", ");
        }
        printf("]\n");
    }
}
int find_in_writebuffer(uint32_t addr){
    for(int i=0;i<WRITE_BUFFER_SIZE;i++){
        if(write_buffer[i].valid&&write_buffer[i].addr==addr){
            return i;
        }
    }
    return -1;
}
void fwd(uint32_t idx, uint32_t data[DCACHE_OFFSET_NUM]){
    for(int j=0;j<DCACHE_OFFSET_NUM;j++){
        data[j] = write_buffer[idx].data[j];
    }
}
bool writebuffer_find(uint32_t addr,uint32_t offset, uint32_t& data)
{
    // if(DCACHE_LOG){
    //     printf("WriteBuffer Find Request: addr=0x%08X offset=0x%02X\n", addr, offset);
    //     for(int i=0;i<WRITE_BUFFER_SIZE;i++){
    //         printf("  Entry[%d]: valid=%d addr=0x%08X data=[0x%08X, 0x%08X, 0x%08X, 0x%08X]\n", i, write_buffer[i].valid, write_buffer[i].addr,
    //                write_buffer[i].data[0], write_buffer[i].data[1],
    //                write_buffer[i].data[2], write_buffer[i].data[3]);
    //     }
    // }
    for (int i = 0; i < WRITE_BUFFER_SIZE; i++)
    {
        if (write_buffer[i].valid && write_buffer[i].addr == addr)
        {
            data = write_buffer[i].data[offset];
            return true;
        }
    }
    return false;
}