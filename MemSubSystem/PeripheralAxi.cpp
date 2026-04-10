#include "PeripheralAxi.h"
#include "PeripheralModel.h"
#include "config.h"
#include "oracle.h"

namespace {
bool is_local_special_read_addr(uint32_t addr) {
  return addr == OPENSBI_TIMER_LOW_ADDR || addr == OPENSBI_TIMER_HIGH_ADDR;
}

uint32_t local_special_read_data(uint32_t addr) {
  if (addr == OPENSBI_TIMER_LOW_ADDR) {
#ifdef CONFIG_BPU
    return static_cast<uint32_t>(sim_time);
#else
    return static_cast<uint32_t>(get_oracle_timer());
#endif
  }
  if (addr == OPENSBI_TIMER_HIGH_ADDR) {
#ifdef CONFIG_BPU
    return static_cast<uint32_t>(sim_time >> 32);
#else
    return static_cast<uint32_t>(get_oracle_timer());
#endif
  }
  return 0;
}
} // namespace

void PeripheralAxi::init() {
  in = {};
  out = {};
  cur = {};
  nxt = {};
  if (peripheral_resp != nullptr) {
    *peripheral_resp = {};
  }
}

uint8_t PeripheralAxi::access_size_bytes(uint8_t func3) {
  switch (func3 & 0x3u) {
  case 0:
    return 1;
  case 1:
    return 2;
  default:
    return 4;
  }
}

uint32_t PeripheralAxi::pack_write_data(uint32_t data, uint8_t func3) {
  switch (func3 & 0x3u) {
  case 0:
    return data & 0xFFu;
  case 1:
    return data & 0xFFFFu;
  default:
    return data;
  }
}

uint64_t PeripheralAxi::pack_write_strb(uint8_t func3) {
  switch (func3 & 0x3u) {
  case 0:
    return 0x1ull;
  case 1:
    return 0x3ull;
  default:
    return 0xFull;
  }
}

uint32_t PeripheralAxi::extract_read_data(uint32_t raw, uint8_t func3) {
  switch (func3) {
  case 0b000: {
    uint32_t val = raw & 0xFFu;
    return (val & 0x80u) ? (val | 0xFFFFFF00u) : val;
  }
  case 0b001: {
    uint32_t val = raw & 0xFFFFu;
    return (val & 0x8000u) ? (val | 0xFFFF0000u) : val;
  }
  case 0b100:
    return raw & 0xFFu;
  case 0b101:
    return raw & 0xFFFFu;
  default:
    return raw;
  }
}

void PeripheralAxi::comb_outputs() {
  out = {};

  if (peripheral_resp != nullptr) {
    *peripheral_resp = {};
    peripheral_resp->ready = !cur.busy;
    if (cur.resp_valid) {
      peripheral_resp->is_mmio = 1;
      peripheral_resp->mmio_rdata = cur.rdata;
      peripheral_resp->uop = cur.uop;
    }
  }

  if (!cur.busy) {
    return;
  }

  if (!cur.req_accepted) {
    const uint8_t total_size = static_cast<uint8_t>(access_size_bytes(cur.func3) - 1u);
    if (cur.write) {
      out.write.req_valid = true;
      out.write.req_addr = cur.addr;
      out.write.req_total_size = total_size;
      out.write.req_id = cur.req_id;
      out.write.req_wstrb = pack_write_strb(cur.func3);
      out.write.req_wdata[0] = pack_write_data(cur.wdata, cur.func3);
    } else {
      out.read.req_valid = true;
      out.read.req_addr = cur.addr;
      out.read.req_total_size = total_size;
      out.read.req_id = cur.req_id;
    }
    return;
  }

  if (cur.write) {
    out.write.resp_ready = true;
  } else {
    out.read.resp_ready = true;
  }
}

void PeripheralAxi::comb_inputs() {
  nxt = cur;

  if (cur.resp_valid) {
    nxt.resp_valid = false;
  }

  if (!cur.busy) {
    if (peripheral_req != nullptr && peripheral_req->is_mmio) {
      if (!peripheral_req->wen &&
          is_local_special_read_addr(peripheral_req->mmio_addr)) {
        nxt.busy = false;
        nxt.write = false;
        nxt.req_accepted = false;
        nxt.resp_valid = true;
        nxt.addr = peripheral_req->mmio_addr;
        nxt.wdata = 0;
        nxt.func3 = 0;
        nxt.rdata = local_special_read_data(peripheral_req->mmio_addr);
        nxt.req_id = 0;
        nxt.uop = peripheral_req->uop;
        nxt.uop.dbg.difftest_skip = true;
        return;
      }
      if (peripheral_model != nullptr &&
          PeripheralModel::is_modeled_mmio(peripheral_req->mmio_addr)) {
        nxt.busy = false;
        nxt.write = peripheral_req->wen;
        nxt.req_accepted = false;
        nxt.resp_valid = true;
        nxt.addr = peripheral_req->mmio_addr;
        nxt.wdata = peripheral_req->mmio_wdata;
        nxt.func3 = peripheral_req->uop.func3;
        nxt.rdata = peripheral_req->wen
                        ? 0
                        : peripheral_model->read_load(peripheral_req->mmio_addr,
                                                      nxt.func3);
        nxt.req_id = 0;
        nxt.uop = peripheral_req->uop;
        return;
      }
      nxt.busy = true;
      nxt.write = peripheral_req->wen;
      nxt.req_accepted = false;
      nxt.resp_valid = false;
      nxt.addr = peripheral_req->mmio_addr;
      nxt.wdata = peripheral_req->mmio_wdata;
      nxt.func3 = peripheral_req->uop.func3;
      nxt.rdata = 0;
      nxt.req_id = 0;
      nxt.uop = peripheral_req->uop;
    }
    return;
  }

  if (!cur.req_accepted) {
    if ((cur.write && in.write.req_accepted) ||
        (!cur.write && in.read.req_accepted)) {
      nxt.req_accepted = true;
    }
    return;
  }

  if (cur.write) {
    if (in.write.resp_valid) {
      nxt.busy = false;
      nxt.write = false;
      nxt.req_accepted = false;
      nxt.resp_valid = true;
      nxt.rdata = 0;
    }
    return;
  }

  if (in.read.resp_valid) {
    nxt.busy = false;
    nxt.write = false;
    nxt.req_accepted = false;
    nxt.resp_valid = true;
    nxt.rdata = extract_read_data(in.read.resp_data[0], cur.func3);
  }

  if (peripheral_resp != nullptr) {
    peripheral_resp->ready = !nxt.busy;
  }
}

void PeripheralAxi::seq() {
  cur = nxt;
  nxt = cur;
}
