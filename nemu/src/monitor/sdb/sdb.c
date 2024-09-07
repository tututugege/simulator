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

#include "sdb.h"
#include "utils.h"
#include "watchpoint.h"
#include <cpu/cpu.h>
#include <isa.h>
#include <memory/vaddr.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/select.h>

static int is_batch_mode = false;

bool make_token(char *e);

extern Token *tokens;
extern int nr_token;
extern Token tokens_p[32];

int div_zero = 0;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin.
 */
static char *rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_si(char *args);
static int cmd_info(char *args);
static int cmd_x(char *args);
static int cmd_p(char *args);
static int cmd_w(char *args);
static int cmd_d(char *args);

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}

static int cmd_q(char *args) { return -1; }

static int cmd_help(char *args);

static struct {
  const char *name;
  const char *description;
  int (*handler)(char *);
} cmd_table[] = {
    {"help", "Display information about all supported commands", cmd_help},
    {"c", "Continue the execution of the program", cmd_c},
    {"q", "Exit NEMU", cmd_q},

    /* TODO: Add more commands */
    {"si", "execute N steps", cmd_si},
    {"info", "display infomation NEMU", cmd_info},
    {"x", "scan memory", cmd_x},
    {"p", "expression evaluation", cmd_p},
    {"w", "create watchpoint", cmd_w},
    {"d", "delete watchpoint", cmd_d},

};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  } else {
    for (i = 0; i < NR_CMD; i++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

static int cmd_si(char *args) {
  char *arg = strtok(NULL, " ");
  int step;

  if (arg == NULL) {
    step = 1;
  } else {
    step = atoi(arg);

    if (step == 0) {
      printf("Error steps: %s", arg);
      return 0;
    }
  }

  cpu_exec(step);
  return 0;
}

static int cmd_info(char *args) {
  char *arg = strtok(NULL, " ");

  if (arg == NULL) {
    printf("pass \"r\" to print register info or \"w\" to print watchpoints "
           "info\n");
  } else if (strcmp(arg, "r") == 0) {
    isa_reg_display();
  } else if (strcmp(arg, "w") == 0) {
    display_wp();
  } else {
    printf("Unknown command: %s\n", arg);
  }

  return 0;
}

static int cmd_x(char *args) {
  char *arg = strtok(NULL, " ");
  int size;
  int addr;

  if (arg == NULL) {
    printf("Argument required\n");
    return 0;
  } else if ((size = atoi(arg)) == 0) {
    printf("Unknown command: %s\n", arg);
    return 0;
  }

  arg = strtok(NULL, " ");

  if (arg == NULL) {
    printf("Argument required\n");
    return 0;
  }

  bool success = true;
  tokens = tokens_p;
  addr = expr(arg, &success);

  if (success == false)
    return 0;

  for (int i = 0; i < size; i += 4) {
    printf("%08x: ", addr + i * 4);
    for (int j = 0; j < 4; j++) {
      printf("0x%08x ", vaddr_read(addr + (i + j) * 4, 4));
    }
    printf("\n");
  }

  return 0;
}

static int cmd_p(char *args) {
  if (args[0] == '\0') {
    printf("Argument required\n");
    return 0;
  }

  bool success = true;
  int res;
  tokens = tokens_p;
  res = expr(args, &success);
  if (success == true) {
    printf("%d\n", res);
  }

  return 0;
}

static int cmd_w(char *args) {
  if (args[0] == '\0') {
    printf("Argument required\n");
    return 0;
  }

  bool success = true;

  WP *wp = new_wp();
  if (wp == NULL) {
    printf("No more watchpoint\n");
    return 0;
  }
  tokens = wp->tokens;
  wp->old_val = expr(args, &success);
  wp->nr_tokens = nr_token;

  if (success == false) {
    free_wp(wp);
  } else {
    printf("watchpoint %d: %s\n", wp->NO, args);
  }
  return 0;
}

static int cmd_d(char *args) {

  char *arg = strtok(NULL, " ");
  int wp_no;

  if (arg == NULL) {
    printf("Argument required\n");
    return 0;
  } else if ((wp_no = atoi(arg)) == 0 && strcmp(arg, "0") != 0) {
    printf("Unknown command: %s\n", arg);
    return 0;
  }

  de_wp(wp_no);

  return 0;
}

void sdb_set_batch_mode() { is_batch_mode = true; }

#define LOOP 100
int last_cmd = -1;
void sdb_mainloop() {
  if (is_batch_mode) {
    cmd_c(NULL);

    // check expr
    //   FILE *fp = fopen("tools/gen-expr/test.txt", "r");
    //   assert(fp != NULL);
    //   char buf[1000];
    //   int buf_idx;
    //   int ret, fret;
    //   int reference, result;
    //   bool success;
    //   int ch;
    //
    //   for (int i = 0; i < LOOP; i++) {
    //     success = true;
    //     buf_idx = 0;
    //     fret = fscanf(fp, "%d %d", &ret, &reference);
    //     assert(fret == 2);
    //     fgetc(fp);
    //     while ((ch = fgetc(fp)) != '\n') {
    //       buf[buf_idx++] = ch;
    //     }
    //     buf[buf_idx] = '\0';
    //
    //     result = expr(buf, &success);
    //     if (ret == -1) {
    //       assert(div_zero == 1 && success == false);
    //       div_zero = 0;
    //     } else {
    //       printf("%d. %s ref: %d res: %d\n", i, buf, reference, result);
    //       assert(result == reference && success == true);
    //     }
    //   }
    //
    //   printf("success!\n");
    return;
  }

  for (char *str; (str = rl_gets()) != NULL;) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL && last_cmd != -1) {
      cmd_table[last_cmd].handler(NULL);
      continue;
    }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        last_cmd = i;
        if (cmd_table[i].handler(args) < 0) {
          nemu_state.state = NEMU_QUIT;
          return;
        }
        break;
      }
    }

    if (i == NR_CMD) {
      printf("Unknown command '%s'\n", cmd);
    }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
