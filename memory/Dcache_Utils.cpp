#include "Dcache_Utils.h"
#include "WriteBuffer.h"
#include <cstdio>

extern long long sim_time;

long long total_num;
long long miss_num;
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

void   hit_check(uint32_t index, uint32_t tag, bool &hit, int &way_idx,uint32_t &hit_data,uint32_t tag_check[DCACHE_WAY_NUM],uint32_t data_check[DCACHE_WAY_NUM]) {
    if(DCACHE_LOG){
        // printf("Dcache Hit Check: index=0x%02X, tag=0x%08x\n", index, tag);
        // for(int i=0;i<DCACHE_WAY_NUM;i++){
        //     printf("  Way[%d]: Valid=%d Tag=0x%08X Data=0x%08X\n", i, dcache_valid[index][i], tag_check[i], data_check[i]);
        // }
    }
    for (int i = 0; i < DCACHE_WAY_NUM; i++) {
        // if(DCACHE_LOG){
        //     printf("  Way[%d]: Valid=%d Tag=0x%08X Data=0x%08X\n", i, dcache_valid[index][i], tag_check[i], data_check[i]);
        // }
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
    if(DCACHE_LOG){
        printf("Dcache Write Data Mask: old_data=0x%08x, wdata=0x%08x, wstrb=0x%X, new_data=0x%08x\n", old_data, wdata, wstrb, new_data);
    }
    return new_data;
}

void tag_and_data_read(uint32_t index,uint32_t offset,uint32_t tag[DCACHE_WAY_NUM], uint32_t data[DCACHE_WAY_NUM])
{
    for(int i=0;i<DCACHE_WAY_NUM;i++){
        tag[i] = dcache_tag[index][i];
        data[i] = dcache_data[index][i][offset];
    }
    // if(DCACHE_LOG){
    //     printf("Dcache Tag and Data Read: index=0x%02X, offset=0x%02X\n", index, offset);
    //     for(int i=0;i<DCACHE_WAY_NUM;i++){
    //         printf("  Way[%d]: Tag=0x%08X Data=0x%08X\n", i, tag[i], data[i]);
    //     }
    // }
}
bool dcache_read(uint32_t addr, uint32_t &rdata){
    uint32_t index = GET_INDEX(addr);
    uint32_t tag = GET_TAG(addr);
    uint32_t offset = GET_OFFSET(addr);

    uint32_t tag_check[DCACHE_WAY_NUM];
    uint32_t data_check[DCACHE_WAY_NUM];
    tag_and_data_read(index,offset,tag_check,data_check);

    if(DCACHE_LOG){
        // printf("Dcache Read Request: addr=0x%08X index=0x%02X tag=0x%08X offset=0x%02X\n", addr, index, tag, offset);
        // for(int i=0;i<DCACHE_WAY_NUM;i++){
        //     printf("  Way[%d]: Valid=%d Tag=0x%08X Data=0x%08X\n", i, dcache_valid[index][i], tag_check[i], data_check[i]);
        // }
    }

    bool hit = false;
    int way_idx = 0;
    uint32_t hit_data = 0;
    hit_check(index, tag, hit, way_idx, hit_data, tag_check, data_check);

    if(hit){
        rdata = hit_data;
        updatelru(index, way_idx);
        if(DCACHE_LOG){
            printf("Dcache Read Hit: addr=0x%08X rdata=0x%08X way=%d sim_time:%lld\n", addr, rdata, way_idx, sim_time);
        }
        return true;
    }
    else{
        hit = writebuffer_find(GET_ADDR(tag,index,0), offset, rdata);
        if(hit){
            if(DCACHE_LOG){
                printf("Dcache Read Hit in WriteBuffer: addr=0x%08X rdata=0x%08X sim_time:%lld\n", addr, rdata, sim_time);
            }
            return true;
        } 
        if(DCACHE_LOG){
            printf("Dcache Read Miss: addr=0x%08X sim_time:%lld\n", addr, sim_time);
        }
        return false;
    }
} 