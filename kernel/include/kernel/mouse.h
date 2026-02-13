#ifndef _KERNEL_MOUSE_H
#define _KERNEL_MOUSE_H

#include <stdint.h>

#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

void mouse_initialize(void);
int  mouse_get_x(void);
int  mouse_get_y(void);
uint8_t mouse_get_buttons(void);
void mouse_get_delta(int *dx, int *dy);
int  mouse_poll(void);

#endif
