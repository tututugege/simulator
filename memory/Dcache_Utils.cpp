#include "Dcache_Utils.h"
#include <cstdio>

extern long long sim_time;
uint32_t dcache_data[DCACHE_LINE_NUM][DCACHE_WAY_NUM][DCACHE_OFFSET_NUM] = {0};
uint32_t dcache_lru[DCACHE_LINE_NUM][DCACHE_WAY_NUM] = {0};
uint32_t dcache_tag[DCACHE_LINE_NUM][DCACHE_WAY_NUM] = {0};
bool dcache_valid[DCACHE_LINE_NUM][DCACHE_WAY_NUM] = {0};
bool dcache_dirty[DCACHE_LINE_NUM][DCACHE_WAY_NUM] = {0};

void updatelru(int linenum,int way)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           
{
    for (int i = 0; i < DCACHE_WAY_NUM; i++)
    {
        dcache_lru[linenum][i]++;
    }
    dcache_lru[linenum][way] = 0;
}
int getlru(int linenum)
{ 
    int imax = -1;
    int way = -1;
    for (int i = 0; i < DCACHE_WAY_NUM; i++)
    {
        if ((int)dcache_lru[linenum][i] > imax)
        {
            imax = dcache_lru[linenum][i];
            way = i;
        }
    }
    dcache_lru[linenum][way] = 0;
    return way;
}

void hit_check(uint32_t index, uint32_t tag, bool &hit, uint32_t &way_idx,uint32_t &hit_data,uint32_t tag_check[DCACHE_WAY_NUM],uint32_t data_check[DCACHE_WAY_NUM]) {
    for (int i = 0; i < DCACHE_WAY_NUM; i++) {
        if(dcache_valid[index][i] && tag_check[i] == tag){
            hit = true;
            way_idx = i;
            hit_data = data_check[i];
            break;
        }
    }
}

uint32_t write_data_mask(uint32_t old_data, uint32_t wdata, uint32_t wstrb) {
    uint32_t mask = 0;
    if (wstrb & 0b1)
        mask |= 0xFF;
    if (wstrb & 0b10)
        mask |= 0xFF00;
    if (wstrb & 0b100)
        mask |= 0xFF0000;
    if (wstrb & 0b1000)
        mask |= 0xFF000000;
    uint32_t new_data = (mask & wdata) | (~mask & old_data);
    return new_data;
}

void tag_and_data_read(uint32_t index,uint32_t offset,uint32_t tag[DCACHE_WAY_NUM], uint32_t data[DCACHE_WAY_NUM])
{
    for(int i=0;i<DCACHE_WAY_NUM;i++){
        tag[i] = dcache_tag[index][i];
        data[i] = dcache_data[index][i][offset];
    }
}