#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t ata_base_port;
extern char detected_model[41];

void read_sector(uint32_t lba, uint16_t* buf);
void write_sector(uint32_t lba, uint16_t* buf);
int identify_drive(uint16_t port);
void scan_all_disks(void);
void check_disk_health(void);
void disk_menu(void);
void restore_disk_brute(void);
void init_disk_system(void);

#ifdef __cplusplus
}
#endif

#endif