#pragma once
#include <assert.h>
#include <vector>

template <typename T> class to_FIFO {
public:
  std::vector<bool> we;
  std::vector<bool> re;
  std::vector<T> wdata;
};

template <typename T> class from_FIFO {
public:
  std::vector<T> rdata;
};

template <typename T> class FIFO {
public:
  FIFO(int r_num, int w_num, int depth, int width) {
    this->depth = depth;
    this->width = width;
    this->r_num = r_num;
    this->w_num = w_num;
    assert(r_num > 0 && w_num > 0);

    to_fifo.we.resize(w_num, false);
    to_fifo.wdata.resize(w_num);

    to_fifo.re.resize(r_num, false);
    from_fifo.rdata.resize(r_num);
    data.resize(depth);
  }
  void read() {

    for (int i = 0; i < r_num; i++) {
      if (to_fifo.re[i]) {
        from_fifo.rdata[i] = data[deq_ptr_1];
        deq_ptr_1 = (deq_ptr_1 + 1) % depth;
      }
    }
  }

  void write() {
    for (int i = 0; i < w_num; i++) {
      if (to_fifo.we[i]) {
        data[enq_ptr] = to_fifo.wdata[i];
        enq_ptr = (enq_ptr + 1) % depth;
      }
    }
    deq_ptr = deq_ptr_1;
  }

  to_FIFO<T> to_fifo;
  from_FIFO<T> from_fifo;

  int deq_ptr = 0;
  int enq_ptr = 0;
  int deq_ptr_1 = 0;

private:
  int depth;
  int width;
  int r_num;
  int w_num;

  std::vector<T> data;
};
