#include "MSHR.h"
#include <cstdio>

mshr_entry mshr_entries[MSHR_ENTRY_SIZE];
table_entry mshr_table[MSHR_TABLE_SIZE];
enum MSHR_STATE mshr_state = MSHR_IDLE;

void MSHR::comb_out(){
    
}
void MSHR::comb_ready(){
    out.mshr2dcache_ready->ready = count_mshr < MSHR_ENTRY_SIZE - 1 & count_table < MSHR_TABLE_SIZE - 1;
}
void MSHR::comb(){
    
}
void MSHR::seq(){
    if(in.control->mispred){
        for(int i=0;i<count_table;i++){
            uint32_t idx = (table_head + i) % MSHR_TABLE_SIZE;
            if (mshr_table[idx].valid&&mshr_table[idx].type==0)
            {
                if(in.control->br_mask & (1<<mshr_table[idx].uop.tag)){
                    table_free(idx);
                }
            }
        }
    }
    if(in.control->flush){
        for(int i=0;i<count_table;i++){
            uint32_t idx = (table_head + i) % MSHR_TABLE_SIZE;
            if(mshr_table[idx].valid&&mshr_table[idx].type==0){
                table_free(idx);
            }
        }
    }

    if(mshr_state == MSHR_IDLE){
        if(mshr_count > 0){
            mshr_head = (mshr_head + 1) % MSHR_ENTRY_SIZE;
            if(mshr_entries[mshr_head].valid){
                if(mshr_entries[mshr_head].count > 0){
                    mshr_entries[mshr_head].issued = true;

                    mshr_state = MSHR_DEAL;
                }
                else{
                    mshr_entries[mshr_head].valid = false;
                }
            }
        }
    }
    else if(mshr_state == MSHR_DEAL){

        
    }else if(mshr_state == MSHR_TRAN){

    }
}
void MSHR::table_free(uint32_t idx){
    mshr_table[idx].valid = false;
    mshr_entries[mshr_table[idx].entry].count--;
    count_table--;
}