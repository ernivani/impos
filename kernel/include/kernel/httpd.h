#ifndef _KERNEL_HTTPD_H
#define _KERNEL_HTTPD_H

void httpd_initialize(void);
int  httpd_start(void);
void httpd_stop(void);
void httpd_poll(void);
int  httpd_is_running(void);

#endif
