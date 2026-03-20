#include "writebuffer.h"
#include <cassert>
#include <cstdio>
#include <cstring>

long long sim_time = 0;
uint32_t *p_memory = nullptr;

int main() {
  WriteBuffer wb;
  wb.init();

  constexpr uint32_t kLineAddr = 0x1000;
  uint32_t line_data[DCACHE_LINE_WORDS] = {};
  for (int i = 0; i < DCACHE_LINE_WORDS; i++) {
    line_data[i] = 0x10000000u + static_cast<uint32_t>(i);
  }

  wb.in.clear();
  wb.in.mshrwb.valid = true;
  wb.in.mshrwb.addr = kLineAddr;
  std::memcpy(wb.in.mshrwb.data, line_data, sizeof(line_data));
  wb.comb_outputs();
  wb.comb_inputs();
  wb.seq();
  sim_time++;

  assert(wb.cur.count == 1);
  assert(write_buffer[wb.cur.head].valid);

  constexpr uint32_t kMergedWord = 0xDEADBEEFu;
  wb.in.clear();
  wb.in.dcachewb.merge_req[0].valid = true;
  wb.in.dcachewb.merge_req[0].addr = kLineAddr + 12;
  wb.in.dcachewb.merge_req[0].data = kMergedWord;
  wb.in.dcachewb.merge_req[0].strb = 0xF;
  wb.comb_outputs();
  wb.comb_inputs();
  wb.seq();
  sim_time++;

  assert(write_buffer[wb.cur.head].data[3] == kMergedWord);

  wb.in.clear();
  wb.in.dcachewb.bypass_req[0].valid = true;
  wb.in.dcachewb.bypass_req[0].addr = kLineAddr + 12;
  wb.comb_outputs();
  wb.comb_inputs();
  wb.seq();
  sim_time++;

  wb.in.clear();
  wb.comb_outputs();
  assert(wb.out.wbdcache.bypass_resp[0].valid);
  assert(wb.out.wbdcache.bypass_resp[0].data == kMergedWord);

  wb.in.clear();
  wb.in.axi_in.req_ready = true;
  wb.comb_outputs();
  assert(wb.out.axi_out.req_valid);
  assert(wb.out.axi_out.req_addr == kLineAddr);
  assert(wb.out.axi_out.req_wdata[3] == kMergedWord);
  wb.comb_inputs();
  wb.seq();
  sim_time++;

  assert(wb.cur.count == 1);
  assert(wb.cur.send == 1);

  wb.in.clear();
  wb.in.axi_in.resp_valid = true;
  wb.comb_outputs();
  wb.comb_inputs();
  wb.seq();
  sim_time++;

  assert(wb.cur.count == 0);
  assert(wb.cur.send == 0);

  std::printf("writebuffer standalone test passed\n");
  return 0;
}
