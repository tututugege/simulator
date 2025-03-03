#ifndef _TAGE_FUNC_H_
#define _TAGE_FUNC_H_

bool *tage_get_randin_cal(int func_id, bool *In);
bool *tage_get_input(int func_id);

#define IN1_LENGTH 416
#define IN2_LENGTH 104
#define IN3_LENGTH 195
#define IN4_LENGTH 513

#define OUT1_LENGTH 192
#define OUT2_LENGTH 18
#define OUT3_LENGTH 280
#define OUT4_LENGTH 512

// extern bool IO1_buf[608];
// extern bool IO2_buf[122];
// extern bool IO3_buf[475];
// extern bool IO4_buf[1025];

#endif
