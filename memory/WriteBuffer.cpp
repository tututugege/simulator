#include "WriteBuffer.h"
#include <cstdio>

WriteBuffer_entry write_buffer[WRITE_BUFFER_SIZE];

void WriteBuffer::init(){
    head = 0;
    tail = 0;
    count = 0;
}
void WriteBuffer::comb_ready(){
    out.writebuffer2dcache->ready = (count < WRITE_BUFFER_SIZE);
}
void WriteBuffer::comb(){
    if(write_buffer[head].valid){
        out.writebuffer2arbiter_control->en = true;
        out.writebuffer2arbiter_control->wen = true;
        out.writebuffer2arbiter_control->wdata = write_buffer[head].data[wdone_count]; // Simplified for demo
        out.writebuffer2arbiter_control->addr = write_buffer[head].addr;
        out.writebuffer2arbiter_control->len = DCACHE_OFFSET_NUM - 1;
        out.writebuffer2arbiter_control->size = 0b10;
        out.writebuffer2arbiter_control->sel = 0b1111;
        out.writebuffer2arbiter_control->done = wdone & !in.arbiter2writebuffer_data->done;
        out.writebuffer2arbiter_control->last = offset == DCACHE_OFFSET_NUM - 1 & wdone;
    }
}
void WriteBuffer::seq(){
    if(in.arbiter2writebuffer_data->last){
        write_buffer[head].valid = 0;
        head = (head + 1) % WRITE_BUFFER_SIZE;
        count--;
    }

}