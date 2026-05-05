#ifndef DOS_EMU_H
#define DOS_EMU_H

int dos_load_com(const char* filename);
int dos_load_exe(const char* filename);
void dos_run(void);
void dos_stop(void);
void dos_test();

#endif