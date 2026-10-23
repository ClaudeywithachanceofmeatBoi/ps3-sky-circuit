#ifndef SKY_LOG_H
#define SKY_LOG_H

/* Dual log: /dev_hdd0/tmp/skycircuit.log and USB root (first found). */
void log_init(void);
void log_msg(const char *fmt, ...);
void log_flush(void);

#endif
