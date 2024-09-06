/***************************************************************************************
 * Copyright (c) 2014-2022 Zihao Yu, Nanjing University
 *
 * NEMU is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan
 *PSL v2. You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 *KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 *NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 ***************************************************************************************/

#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <assert.h>
#include <common.h>
#include <device/map.h>
#include <stdint.h>

enum {
  reg_freq,
  reg_channels,
  reg_samples,
  reg_sbuf_size,
  reg_init,
  reg_count,
  nr_reg
};

static uint8_t *sbuf = NULL;
static uint32_t *audio_base = NULL;

static void audio_play(void *userdata, uint8_t *stream, int len) {
  static int rd_ptr = 0;
  // printf("play");
  int nread = len;
  if (audio_base[reg_count] < len)
    nread = audio_base[reg_count];

  // if (nread)
  //   printf("rd_ptr: %d nread: %d len: %d\n", rd_ptr, nread, len);

  int size = audio_base[reg_sbuf_size];
  int end_ptr = (rd_ptr + nread) % size;
  while (rd_ptr != end_ptr) {
    *(stream++) = sbuf[rd_ptr];
    rd_ptr = (rd_ptr + 1) % size;
  }

  if (len > nread) {
    memset(stream + nread, 0, len - nread);
  }

  audio_base[reg_count] -= nread;
}

static void audio_io_handler(uint32_t offset, int len, bool is_write) {
  static SDL_AudioSpec s = {
      .format = AUDIO_S16SYS, .userdata = NULL, .callback = audio_play};

  assert(offset % 4 == 0);
  if (is_write && offset == reg_init * sizeof(uint32_t) &&
      audio_base[reg_init]) {
    s.freq = audio_base[reg_freq];
    s.channels = audio_base[reg_channels];
    s.samples = audio_base[reg_samples];

    int ret = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (ret == 0) {
      SDL_OpenAudio(&s, NULL);
      SDL_PauseAudio(0);
    }
  }
}

static void sbuf_io_handler(uint32_t offset, int len, bool is_write) {
  if (is_write) {
    audio_base[reg_count] += len;
  }
}

void init_audio() {
  uint32_t space_size = sizeof(uint32_t) * nr_reg;
  audio_base = (uint32_t *)new_space(space_size);
#ifdef CONFIG_HAS_PORT_IO
  add_pio_map("audio", CONFIG_AUDIO_CTL_PORT, audio_base, space_size,
              audio_io_handler);
#else
  add_mmio_map("audio", CONFIG_AUDIO_CTL_MMIO, audio_base, space_size,
               audio_io_handler);
#endif

  sbuf = (uint8_t *)new_space(CONFIG_SB_SIZE);
  add_mmio_map("audio-sbuf", CONFIG_SB_ADDR, sbuf, CONFIG_SB_SIZE,
               sbuf_io_handler);

  audio_base[reg_sbuf_size] = CONFIG_SB_SIZE;
}
