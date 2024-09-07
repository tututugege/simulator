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

#include "common.h"
#include <isa.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

int eval(int p, int q, bool *success);
word_t vaddr_read(vaddr_t addr, int len);

enum {
  TK_NOTYPE = 256,
  TK_EQ,
  TK_NEQ,
  TK_AND,

  /* TODO: Add more token types */
  TK_DEC,
  TK_HEX,
  TK_NEG,
  TK_PTR,
  TK_REG

};

extern int div_zero;

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

    /* TODO: Add more rules.
     * Pay attention to the precedence level of different rules.
     */

    {" +", TK_NOTYPE},             // spaces
    {"\\+", '+'},                  // plus
    {"\\-", '-'},                  // sub
    {"\\*", '*'},                  // mul
    {"/", '/'},                    // div
    {"0[xX][0-9a-fA-F]+", TK_HEX}, // hex
    {"[0-9]+", TK_DEC},            // decimal
    {"\\(", '('},                  // left parenthese
    {"\\)", ')'},                  // right parenthese
    {"\\$[0-9a-zA-Z]+", TK_REG},   // register
    {"==", TK_EQ},                 // equal
    {"!=", TK_NEQ},                // neq

};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
  // int num;
} Token;

Token tokens_p[32] __attribute__((used)) = {};
Token *tokens = tokens_p;
int nr_token __attribute__((used)) = 0;

bool make_token(char *e) {
  int position = 0;
  int i;

  regmatch_t pmatch;
  nr_token = 0;
  int last_type = TK_NOTYPE;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 &&
          pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s", i,
            rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type) {
        case TK_NOTYPE:
          break;
        case '+':
        case '/':
        case '(':
        case ')':
        case TK_EQ:
        case TK_NEQ:
          tokens[nr_token++].type = rules[i].token_type;
          last_type = rules[i].token_type;
          break;

        case '*':
        case '-':
          if (last_type != TK_DEC && last_type != TK_HEX && last_type != ')')
            tokens[nr_token++].type =
                (rules[i].token_type == '-') ? TK_NEG : TK_PTR;
          else
            tokens[nr_token++].type = rules[i].token_type;
          break;
          // case TK_REG:
          //   if (substr_len < 32) {
          //     tokens[nr_token].type = TK_REG;
          //     memcpy(tokens[nr_token].str, substr_start, substr_len);
          //     tokens[nr_token++].str[substr_len] = '\0';
          //     last_type = rules[i].token_type;
          //   } else {
          //     printf("Error register name\n");
          //     return false;
          //   }
          //   break;

        default:
          if (substr_len < 32) {
            tokens[nr_token].type = rules[i].token_type;
            memcpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token++].str[substr_len] = '\0';
            last_type = rules[i].token_type;
          } else {
            printf("Too long\n");
            return false;
          }
        }
        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}

word_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  /* TODO: Insert codes to evaluate the expression. */
  int res = eval(0, nr_token - 1, success);

  return res;
}

/* returning 1 means the outermost parenthese
 * returning 0 means no outermost parenthese
 * returning -1 means error
 */
int check_parenthese(int p, int q) {
  int left_num = 0;

  if (tokens[p].type == '(') {
    left_num++;
    p++;
  } else {
    return 0;
  }

  if (tokens[q].type != ')') {
    return 0;
  }

  while (p < q) {
    if (tokens[p].type == '(')
      left_num++;
    else if (tokens[p].type == ')')
      left_num--;

    if (left_num == 0) {
      return 0;
    } else if (left_num < 0) {
      return -1;
    }
    p++;
  }

  if (left_num == 1)
    return 1;
  else
    return -1;
}

int get_main_op(int p, int q) {
  int last_mul_div = -1;
  int last_add_sub = -1;
  int last_eq_neq = -1;
  int first_neg = -1;
  int first_ptr = -1;
  int left = 0;

  while (p <= q) {
    switch (tokens[p].type) {
    case '(':
      left++;
      for (p++; p <= q; p++) {
        if (tokens[p].type == '(')
          left++;
        else if (tokens[p].type == ')')
          left--;

        if (left == 0)
          break;
      }
      // assert( p != q );
      break;
    case '+':
    case '-':
      last_add_sub = p;
    case '/':
    case '*':
      last_mul_div = p;
    case TK_NEG:
      if (first_neg == -1)
        first_neg = p;
      break;
    case TK_PTR:
      if (first_ptr == -1)
        first_ptr = p;
      break;
    case TK_NEQ:
    case TK_EQ:
      last_eq_neq = p;
      break;

    default:
      assert(tokens[p].type != ')');
      break;
    }
    p++;
  }

  int ret = -1;
  if (last_eq_neq != -1)
    ret = last_eq_neq;
  else if (last_add_sub != -1)
    ret = last_add_sub;
  else if (last_mul_div != -1)
    ret = last_mul_div;
  else if (first_neg != -1)
    ret = first_neg;
  else if (first_ptr != -1)
    ret = first_ptr;

  return ret;
}

int eval(int p, int q, bool *success) {
  int res = 0;

  if (p > q) {
    *success = false;
    printf("Expression format error\n");
    return 0;
  } else if (check_parenthese(p, q) == true) {

    return eval(p + 1, q - 1, success);
  } else if (p == q) {
    assert(tokens[p].type == TK_HEX || tokens[p].type == TK_DEC ||
           tokens[p].type == TK_REG);

    // number
    if (tokens[p].type == TK_HEX)
      res = strtol(tokens[p].str, NULL, 16);
    else if (tokens[p].type == TK_DEC)
      res = strtol(tokens[p].str, NULL, 10);
    // register
    else if (tokens[p].type == TK_REG) {
      res = isa_reg_str2val(tokens[p].str, success);
      if (*success == false)
        printf("Error register name %s\n", tokens[p].str);
    }

  } else {
    int op = get_main_op(p, q);

    if (op == -1) {
      printf("Expression format error\n");
      *success = false;
      return 0;
    }

    int val1 = 0;
    int val2;
    if (tokens[op].type != TK_NEG && tokens[op].type != TK_PTR) {
      val1 = eval(p, op - 1, success);
    }
    val2 = eval(op + 1, q, success);

    if (*success == false)
      return 0;

    switch (tokens[op].type) {
    case '+':
      res = val1 + val2;
      break;
    case '-':
      res = val1 - val2;
      break;
    case '*':
      res = val1 * val2;
      break;
    case '/':
      if (val2 == 0) {
        // printf("Divison by zero\n");
        *success = false;
        div_zero = 1;
        return 0;
      } else {
        res = val1 / val2;
        break;
      }
    case TK_NEG:
      res = -val2;
      break;
    case TK_PTR:
      res = vaddr_read(val2, 4);
      break;
    case TK_EQ:
      res = (val1 == val2);
      break;
    case TK_NEQ:
      res = (val1 != val2);
      break;
    default:
      res = 0;
      assert(0);
    }
  }
  return res;
}
