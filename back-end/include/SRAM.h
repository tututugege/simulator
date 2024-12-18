#pragma once
#include <assert.h>
#include <cstdint>
#include <vector>

template <typename T> class to_SRAM {
public:
  std::vector<bool> we;
  std::vector<uint32_t> waddr;
  std::vector<T> wdata;
  std::vector<uint32_t> raddr;
};

template <typename T> class from_SRAM {
public:
  std::vector<T> rdata;
};

template <typename T> class SRAM {
public:
  SRAM(int r_num, int w_num, int depth, int width) {
    this->depth = depth;
    this->width = width;
    this->r_num = r_num;
    this->w_num = w_num;
    assert(r_num > 0 && w_num > 0);

    to_sram.we.resize(w_num, false);
    to_sram.waddr.resize(w_num);
    to_sram.wdata.resize(w_num);

    to_sram.raddr.resize(r_num);
    from_sram.rdata.resize(r_num);

    data.resize(depth);

    /*if (CAM)*/
    /*  CAM_valid.resize(depth, false);*/
  }
  void read() {
    for (int i = 0; i < r_num; i++) {
      from_sram.rdata[i] = data[to_sram.raddr[i]];
    }
  }
  void write() {
    for (int i = 0; i < w_num; i++) {
      if (to_sram.we[i])
        data[to_sram.waddr[i]] = to_sram.wdata[i];
    }
  }

  to_SRAM<T> to_sram;
  from_SRAM<T> from_sram;

  // debug
  T debug_read(int idx) { return data[idx]; }
  void debug_write(int idx, T data) { data[idx] = data; }

public:
  int depth;
  int width;
  int r_num;
  int w_num;
  std::vector<T> data;
};
