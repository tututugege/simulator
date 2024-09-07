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

#include "watchpoint.h"
#include "common.h"
#include "sdb.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/select.h>

extern Token *tokens;
int eval(int p, int q, bool *success);

static WP wp_pool[NR_WP] = {};
static WP *head = NULL, *free_ = NULL;
static int WP_NO;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i++) {
    wp_pool[i].NO = -1;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
  }

  head = NULL;
  free_ = wp_pool;
  WP_NO = 0;
}

/* TODO: Implement the functionality of watchpoint */

WP *new_wp() {
  WP *p;
  if (free_ == NULL) {
    return NULL;
  }
  p = free_;
  free_ = free_->next;
  p->NO = WP_NO++;

  if (head == NULL) {
    head = p;
    p->next = NULL;
  } else {
    p->next = head;
    head = p;
  }

  return p;
}

void free_wp(WP *wp) {
  assert(wp);

  WP *p, *q;
  q = NULL;

  for (p = head; p && p != wp; q = p, p = p->next)
    ;

  Assert(p, "p is not in head list\n");

  // if p is head
  if (q == NULL) {
    head = wp->next;
  } else {
    q->next = p->next;
  }
  wp->next = free_;
  free_ = wp;
}

bool check_wp() {
  WP *p;
  int res;
  bool change = false;
  bool success = true;
  for (p = head; p; p = p->next) {
    tokens = p->tokens;
    res = eval(0, p->nr_tokens - 1, &success);

    // it must be a correct expression
    assert(success);
    if (p->old_val != res) {
      change = true;
      printf("Old value: %d\n", p->old_val);
      printf("New value: %d\n", res);
      p->old_val = res;
    }
  }
  return change;
}

void de_wp(int no) {
  WP *wp;
  for (wp = head; wp; wp = wp->next) {
    if (wp->NO == no)
      break;
  }

  if (wp == NULL) {
    printf("No watchpoint number %d\n", no);
    return;
  }

  free_wp(wp);
  printf("delete watchpoint %d\n", no);
}

void display_wp() {

  if (head == NULL) {
    printf("No watchpoint\n");
    return;
  }
  WP *wp;
  printf("num\t\tvalue\n");
  for (wp = head; wp; wp = wp->next) {
    printf("%02d\t\t%8d\n", wp->NO, wp->old_val);
  }
}
