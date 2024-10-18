#define LENGTH(arr) (sizeof(arr) / sizeof(arr[0]))
inline void check(int cond) {
  volatile int *end_flag = (int *)0x1c;
  if (!cond) {
    *end_flag = 1;
  }
}

inline void halt(int ret) {
  volatile int *end_flag = (int *)0x1c;
  *end_flag = ret;
}
