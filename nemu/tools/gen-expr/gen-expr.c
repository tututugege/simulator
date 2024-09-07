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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// this should be enough
static char buf[65536] = {};
int buf_idx = 0;
static char code_buf[65536 + 128] = {}; // a little larger than `buf`
static char *code_format = "#include <stdio.h>\n"
                           "int main() { "
                           "  int result = %s; "
                           "  printf(\"%%d\", result); "
                           "  return 0; "
                           "}";

static void gen_num();
static void gen_rand_op();
static void gen(char ch);
static int choose(int n);
static void gen_rand_expr();

int nr_token = 0;

int main(int argc, char *argv[]) {
  int seed = time(0);
  srand(seed);
  int loop = 1;
  if (argc > 1) {
    sscanf(argv[1], "%d", &loop);
  }
  int i;
  // char *str = "0x7fffffffu + 0x7fffffffu";
  for (i = 0; i < loop; i++) {
    buf_idx = 0;
    nr_token = 1;
    gen_rand_expr();
    // memcpy(buf, str, strlen(str) + 1);
    buf[buf_idx] = '\0';

    sprintf(code_buf, code_format, buf);

    FILE *fp = fopen("/tmp/.code.c", "w");
    assert(fp != NULL);
    fputs(code_buf, fp);
    fclose(fp);

    int ret = system("gcc /tmp/.code.c -o /tmp/.expr");
    if (ret != 0)
      continue;

    fp = popen("/tmp/.expr", "r");
    assert(fp != NULL);

    int result;
    ret = fscanf(fp, "%d", &result);
    pclose(fp);

    printf("%d %d %s\n", ret, result, buf);
  }
  return 0;
}

static void gen_rand_expr() {

  assert(nr_token <= 32);
  if (nr_token >= 30) {
    gen_num();
    return;
  }

  switch (choose(6)) {
  case 0:
    gen_num();
    break;
  case 1:
    nr_token += 2;
    gen('(');
    gen_rand_expr();
    gen(')');
    break;
  case 2:
    nr_token += 3;
    gen('(');
    gen('-');
    gen_num();
    gen(')');
    break;
  default:
    nr_token += 2;
    gen_rand_expr();
    gen_rand_op();
    gen_rand_expr();
    break;
  }
}

static void gen_num() {
  int n = rand();
  char str[11];
  sprintf(str, "%d", n);

  int len = strlen(str);
  memcpy(buf + buf_idx, str, len);
  buf_idx += len;
}

static void gen_rand_op() {
  switch (choose(4)) {
  case 0:
    buf[buf_idx++] = '+';
    break;
  case 1:
    buf[buf_idx++] = '-';
    break;
  case 2:
    buf[buf_idx++] = '*';
    break;
  default:
    buf[buf_idx++] = '/';
    break;
  }
}

static void gen(char ch) { buf[buf_idx++] = ch; }

static int choose(int n) { return rand() % n; }
