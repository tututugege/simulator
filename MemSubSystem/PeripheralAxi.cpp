#include "PeripheralAxi.h"

void PeripheralAxi::init() {
  in = {};
  out = {};
  cur = {};
  nxt = {};
  if (peripheral_io != nullptr) {
    peripheral_io->out = {};
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

  if (peripheral_io != nullptr) {
    peripheral_io->out = {};
    peripheral_io->out.ready = !cur.busy;
    if (cur.resp_valid) {
      peripheral_io->out.is_mmio = 1;
      peripheral_io->out.mmio_rdata = cur.rdata;
      peripheral_io->out.uop = cur.uop;
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
    if (peripheral_io != nullptr && peripheral_io->in.is_mmio) {
      nxt.busy = true;
      nxt.write = peripheral_io->in.wen;
      nxt.req_accepted = false;
      nxt.resp_valid = false;
      nxt.addr = peripheral_io->in.mmio_addr;
      nxt.wdata = peripheral_io->in.mmio_wdata;
      nxt.func3 = static_cast<uint8_t>(peripheral_io->in.mmio_wstrb);
      nxt.rdata = 0;
      nxt.req_id = 0;
      nxt.uop = peripheral_io->in.uop;
    }
    return;
  }

  if (!cur.req_accepted) {
    if ((cur.write && in.write.req_ready) || (!cur.write && in.read.req_ready)) {
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

  if (peripheral_io != nullptr) {
    peripheral_io->out.ready = !nxt.busy;
  }
}

void PeripheralAxi::seq() {
  cur = nxt;
  nxt = cur;
}
