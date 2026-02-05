#include "Memory.h"
#include <cstdio>
#include <cstring>

void MEMORY::init() {
  state = MEM_IDLE;
  Latency_cnt = 0;
  data_cnt = 0;
  out.data->done = false;
  out.data->last = false;
}

void MEMORY::comb() {
  if (data_cnt > 0 && in.control->wen == 0) {
    out.data->done = true;
    out.data->last = data_cnt == (uint32_t)in.control->len + 1;
    out.data->data = rdata;

  } else if (state == TRANSFER && in.control->wen == 1) {
    out.data->done = in.control->done;
    out.data->last = in.control->last;
    out.data->data = 0;
  } else {
    out.data->done = false;
    out.data->last = false;
    out.data->data = 0;
  }
}
void MEMORY::seq() {
  if (state == TRANSFER) {
    uint32_t addr_offset =
        (((in.control->addr >> 2) & (0xffffffff << in.control->size)) | data_cnt)
        << 2;

    if (in.control->wen == 0) {
      rdata = p_memory[addr_offset >> 2];
      data_cnt++;
      if (DEBUG) {
        if (addr_offset == DEBUG_ADDR) {
          printf(
              "\nDebug Memory Read at addr:0x%08x rdata:0x%08x sim_time:%lld\n",
              addr_offset, rdata, sim_time);
        }
      }
      if (DCACHE_LOG) {

        printf("\nMemory Read: Addr: 0x%08x Data: 0x%08x\n", addr_offset,
               rdata);
      }
    } else {
      if (in.control->done) {
        uint32_t old_data = p_memory[addr_offset >> 2];
        uint32_t mask = 0;
        if (in.control->sel & 0b1)
          mask |= 0xFF;
        if (in.control->sel & 0b10)
          mask |= 0xFF00;
        if (in.control->sel & 0b100)
          mask |= 0xFF0000;
        if (in.control->sel & 0b1000)
          mask |= 0xFF000000;

        p_memory[addr_offset >> 2] =
            (mask & in.control->wdata) | (~mask & old_data);
        data_cnt++;
        if (DEBUG) {
          if (addr_offset == DEBUG_ADDR) {
            printf("\nDebug Memory Write at addr:0x%08x wdata:0x%08x "
                   "old_data:0x%08x mask:%08x p_memory:0x%08x sim_time:%lld\n",
                   addr_offset, in.control->wdata, old_data, mask,
                   p_memory[addr_offset >> 2], sim_time);
          }
        }
        if (DCACHE_LOG) {
          printf("\nMemory Write: Addr: 0x%08x Data: 0x%08x Wstrb: 0x%X\n",
                 addr_offset, in.control->wdata, in.control->sel);
        }
      }
    }
  }
  if (state == LATENCY) {
    Latency_cnt++;
  } else {
    Latency_cnt = 0;
  }
  if (state != TRANSFER ||
      (in.control->wen == 1 && in.control->done && in.control->last)) {
    data_cnt = 0;
  }

  if (DCACHE_LOG) {
    printf("\nMemory State: state=%d, Latency_cnt=%u, data_cnt=%u\n", state,
           Latency_cnt, data_cnt);
    printf("Memory Control: en=%d, wen=%d, sel=0x%02X, len=%u, done=%d, "
           "last=%d, size=%d, addr=0x%08x, wdata=0x%08x\n",
           in.control->en, in.control->wen, in.control->sel, in.control->len,
           in.control->done, in.control->last, in.control->size,
           in.control->addr, in.control->wdata);
    printf("Memory Output Data: data=0x%08x, last=%d, done=%d\n",
           out.data->data, out.data->last, out.data->done);
  }

  if (in.control->en == true && Latency_cnt == 0 && state == MEM_IDLE) {
    state = LATENCY;
  } else if (in.control->en == true && Latency_cnt == Latency &&
             state == LATENCY) {
    state = TRANSFER;
  } else if (in.control->en == true && in.control->wen == 0 &&
             data_cnt == (uint32_t)in.control->len + 1 && state == TRANSFER) { // AXI优化
    state = MEM_IDLE;
  } else if (in.control->en == true && in.control->wen == 1 &&
             in.control->done && in.control->last && state == TRANSFER) {
    state = MEM_IDLE;
  } else if (in.control->en == false) {
    state = MEM_IDLE;
  }
}
