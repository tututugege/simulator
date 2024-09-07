#include "utils.h"
#include <assert.h>
#include <common.h>
#include <elf.h>

typedef struct {
  char name[32];
  size_t func_start;
  size_t func_end;
} ftrace_node;

#define FTRACE_NODE_NUM 128
ftrace_node ftrace[128];
int nr_ftrace;

void init_elf(const char *elf_file) {
  FILE *fp = fopen(elf_file, "r");
  int ret;
  // Assert(fp, "Can not open '%s'", elf_file);
  if (fp == NULL)
    return;

  Elf32_Ehdr e_head;
  Elf32_Shdr sh_head;
  Elf32_Sym sym_head;
  ret = fread(&e_head, sizeof(Elf32_Ehdr), 1, fp);
  assert(ret);
  Assert(e_head.e_ident[0] == ELFMAG0, "%s it not ELF file", elf_file);

  int symtab_off = 0;
  int sym_num = 0;
  int strtab_off = 0;

  int ch;

  for (int i = 0; i < e_head.e_shnum; i++) {
    fseek(fp, e_head.e_shoff + i * e_head.e_shentsize, SEEK_SET);
    ret = fread(&sh_head, sizeof(Elf32_Shdr), 1, fp);
    assert(ret);

    if (sh_head.sh_type == SHT_SYMTAB) {
      symtab_off = sh_head.sh_offset;
      sym_num = sh_head.sh_size / sizeof(Elf32_Sym);
    } else if (sh_head.sh_type == SHT_STRTAB) {
      strtab_off = sh_head.sh_offset;
    }

    if (symtab_off && strtab_off)
      break;
  }

  for (int i = 0; i < sym_num; i++) {
    fseek(fp, symtab_off + i * sizeof(Elf32_Sym), SEEK_SET);
    ret = fread(&sym_head, sizeof(Elf32_Sym), 1, fp);
    assert(ret);

    if (ELF32_ST_TYPE(sym_head.st_info) == STT_FUNC) {
      ftrace[nr_ftrace].func_start = sym_head.st_value;
      ftrace[nr_ftrace].func_end = sym_head.st_value + sym_head.st_size - 1;
      fseek(fp, strtab_off + sym_head.st_name, SEEK_SET);
      int name_idx = 0;
      while ((ch = fgetc(fp)) != '\0' && name_idx < 31) {
        ftrace[nr_ftrace].name[name_idx++] = ch;
      }
      ftrace[nr_ftrace].name[name_idx] = '\0';
      nr_ftrace++;
      assert(nr_ftrace < FTRACE_NODE_NUM);
    }
  }

  fclose(fp);

  // for (int i = 0; i < nr_ftrace; i++) {
  //   printf("%d %s %lx %lx\n", i, ftrace[i].name, ftrace[i].func_start,
  //          ftrace[i].func_end);
  // }
}

void ftrace_commit(vaddr_t pc, vaddr_t npc) {
  static int depth = 0;
  int now_func = -1;
  int next_func = -1;
  for (int i = 0; i < nr_ftrace; i++) {
    if (npc == ftrace[i].func_start) {
      log_write("0x%x: ", pc);
      for (int j = 0; j < depth; j++)
        log_write(" ");
      log_write("call %s@0x%x\n", ftrace[i].name, npc);
      depth++;
      return;
    }

    if (now_func == -1 && pc >= ftrace[i].func_start &&
        pc <= ftrace[i].func_end)
      now_func = i;
    if (next_func == -1 && npc >= ftrace[i].func_start &&
        npc <= ftrace[i].func_end)
      next_func = i;

    if (next_func != -1 && now_func != -1)
      break;
  }

  if (now_func != next_func) {
    depth--;
    log_write("0x%x: ", pc);
    for (int j = 0; j < depth; j++)
      log_write(" ");

    log_write("return %s@0x%x\n", ftrace[now_func].name, pc);
  }
}
