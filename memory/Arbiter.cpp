#include "Arbiter.h"
#include <cstdio>

void Arbiter::init(){
    state=ARBITER_STATE_IDLE;
}

void Arbiter::comb_in()
{
    if(state==ARBITER_STATE_IDLE){
        out.mem_control->en = false;
        out.mem_control->addr = 0;
        out.mem_control->wdata = 0; 
        out.mem_control->wen = false;
        out.mem_control->sel = 0;
        out.mem_control->len=0;
        out.mem_control->size=0;
        out.mem_control->done=false;
        out.mem_control->last=false;
    }
    else if(state==ARBITER_STATE_STORE_PRIORITIZED){
        out.mem_control->en = in.writebuffer2arbiter_control->en;
        out.mem_control->addr = in.writebuffer2arbiter_control->addr;
        out.mem_control->wdata = in.writebuffer2arbiter_control->wdata;
        out.mem_control->wen = in.writebuffer2arbiter_control->wen;
        out.mem_control->sel = in.writebuffer2arbiter_control->sel;
        out.mem_control->len=in.writebuffer2arbiter_control->len;
        out.mem_control->size=in.writebuffer2arbiter_control->size;
        out.mem_control->last=in.writebuffer2arbiter_control->last;
        out.mem_control->done=in.writebuffer2arbiter_control->done;
        out.mem_control->last=in.writebuffer2arbiter_control->last;
    }else if(state==ARBITER_STATE_LOAD_PRIORITIZED){
        out.mem_control->en = in.mshr2arbiter_control->en;
        out.mem_control->addr = in.mshr2arbiter_control->addr;
        out.mem_control->wdata = in.mshr2arbiter_control->wdata;
        out.mem_control->wen = in.mshr2arbiter_control->wen;
        out.mem_control->sel = in.mshr2arbiter_control->sel;
        out.mem_control->len=in.mshr2arbiter_control->len;
        out.mem_control->size=in.mshr2arbiter_control->size;
        out.mem_control->last=in.mshr2arbiter_control->last;
        out.mem_control->done=in.mshr2arbiter_control->done;
        out.mem_control->last=in.mshr2arbiter_control->last;
        
    }
}
void Arbiter::comb_out()
{
    if(state==ARBITER_STATE_STORE_PRIORITIZED&&in.writebuffer2arbiter_control->en==true){
        out.arbiter2writebuffer_data->data = in.mem_data->data;
        out.arbiter2writebuffer_data->last = in.mem_data->last;
        out.arbiter2writebuffer_data->done = in.mem_data->done;

        out.arbiter2mshr_data->data = 0;
        out.arbiter2mshr_data->last = false;
        out.arbiter2mshr_data->done = false;
    }else if(state==ARBITER_STATE_LOAD_PRIORITIZED&&in.mshr2arbiter_control->en==true){ 
        out.arbiter2mshr_data->data = in.mem_data->data;
        out.arbiter2mshr_data->last = in.mem_data->last;
        out.arbiter2mshr_data->done = in.mem_data->done;

        out.arbiter2writebuffer_data->data = 0;
        out.arbiter2writebuffer_data->last = false;
        out.arbiter2writebuffer_data->done = false;
    }else if(state==ARBITER_STATE_IDLE){
        out.arbiter2writebuffer_data->data = 0;
        out.arbiter2writebuffer_data->last = false;
        out.arbiter2writebuffer_data->done = false;
        out.arbiter2mshr_data->data = 0;
        out.arbiter2mshr_data->last = false;
        out.arbiter2mshr_data->done = false;
    }   
}
void Arbiter::seq()
{
    if(state==ARBITER_STATE_IDLE){
        if(in.writebuffer2arbiter->arbiter_priority==true){
            state=ARBITER_STATE_STORE_PRIORITIZED;
        }
        else if(in.mshr2arbiter->prority==true){
            state=ARBITER_STATE_LOAD_PRIORITIZED;
        }
        else if(in.mshr2arbiter_control->en==true){
            state=ARBITER_STATE_LOAD_PRIORITIZED;
        }
        else if(in.writebuffer2arbiter_control->en==true){
            state=ARBITER_STATE_STORE_PRIORITIZED;
        }
    }else if(state==ARBITER_STATE_STORE_PRIORITIZED){
        if(in.mem_data->last==true){
            state=ARBITER_STATE_IDLE;
        }
    }else if(state==ARBITER_STATE_LOAD_PRIORITIZED){
        if(in.mem_data->last==true||in.mshr2arbiter->prority==false){
            state=ARBITER_STATE_IDLE;
        }
    }
    if(DCACHE_LOG){
        print();
    }
}
void Arbiter::print(){
    printf("\nArbiter State: %u\n", state);
    printf("Arbiter Input WriteBuffer Arbiter: arbiter_priority=%d\n",
           in.writebuffer2arbiter->arbiter_priority);
    printf("Arbiter Input MSHR Arbiter: prority=%d\n",
           in.mshr2arbiter->prority);
    printf("Arbiter Input WriteBuffer Control: en=%d, wen=%d, sel=0x%02X, len=%u, done=%d, last=%d, size=%d, addr=0x%08x, wdata=0x%08x\n",
           in.writebuffer2arbiter_control->en, in.writebuffer2arbiter_control->wen, in.writebuffer2arbiter_control->sel,
           in.writebuffer2arbiter_control->len, in.writebuffer2arbiter_control->done, in.writebuffer2arbiter_control->last,
           in.writebuffer2arbiter_control->size, in.writebuffer2arbiter_control->addr, in.writebuffer2arbiter_control->wdata);
    printf("Arbiter Input MSHR Control: en=%d, wen=%d, sel=0x%02X, len=%u, done=%d, last=%d, size=%d, addr=0x%08x, wdata=0x%08x\n",
           in.mshr2arbiter_control->en, in.mshr2arbiter_control->wen, in.mshr2arbiter_control->sel,
           in.mshr2arbiter_control->len, in.mshr2arbiter_control->done, in.mshr2arbiter_control->last,
           in.mshr2arbiter_control->size, in.mshr2arbiter_control->addr, in.mshr2arbiter_control->wdata);
    printf("Arbiter Input Memory Data: data=0x%08x, last=%d, done=%d\n",
           in.mem_data->data, in.mem_data->last, in.mem_data->done);
    printf("Arbiter Output Memory Control: en=%d, wen=%d, sel=0x%02X, len=%u, done=%d, last=%d, size=%d, addr=0x%08x, wdata=0x%08x\n",
           out.mem_control->en, out.mem_control->wen, out.mem_control->sel,
           out.mem_control->len, out.mem_control->done, out.mem_control->last,
           out.mem_control->size, out.mem_control->addr, out.mem_control->wdata);
    printf("Arbiter Output WriteBuffer Data: data=0x%08x, last=%d, done=%d\n",
           out.arbiter2writebuffer_data->data, out.arbiter2writebuffer_data->last, out.arbiter2writebuffer_data->done);
    printf("Arbiter Output MSHR Data: data=0x%08x, last=%d, done=%d\n",
           out.arbiter2mshr_data->data, out.arbiter2mshr_data->last, out.arbiter2mshr_data->done);
}