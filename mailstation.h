#ifndef __MAILSTATION_H__
#define __MAILSTATION_H__

void ms_setup(void);
int ms_read(void);
int ms_write(char c);
int ms_print(char *string);
int ms_print(String);

#endif
