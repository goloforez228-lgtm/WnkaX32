#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t ata_base_port;
extern char detected_model[41];
extern uint64_t total_sectors;
extern uint64_t free_sectors;

#define AHCI_CAP             0x00
#define AHCI_GHC             0x04
#define AHCI_IS              0x08
#define AHCI_PI              0x0C
#define AHCI_VS              0x10
#define AHCI_CCC_CTL         0x14
#define AHCI_CCC_PORTS       0x18
#define AHCI_EM_LOC          0x1C
#define AHCI_EM_CTL          0x20
#define AHCI_CAP2            0x24
#define AHCI_BOHC            0x28

#define AHCI_PX_CLB          0x00
#define AHCI_PX_CLBU         0x04
#define AHCI_PX_FB           0x08
#define AHCI_PX_FBU          0x0C
#define AHCI_PX_IS           0x10
#define AHCI_PX_IE           0x14
#define AHCI_PX_CMD          0x18
#define AHCI_PX_RESERVED     0x1C
#define AHCI_PX_TFD          0x20
#define AHCI_PX_SIG          0x24
#define AHCI_PX_SSTS         0x28
#define AHCI_PX_SCTL         0x2C
#define AHCI_PX_SERR         0x30
#define AHCI_PX_SACT         0x34
#define AHCI_PX_CI           0x38
#define AHCI_PX_SNTF         0x3C
#define AHCI_PX_FBS          0x40
#define AHCI_PX_DEVSLP       0x44

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved0;
    uint16_t reserved1;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t paddr;
    uint32_t reserved2[4];
} __attribute__((packed)) ahci_cmd_header_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved0;
    uint32_t dbc;
} __attribute__((packed)) ahci_prdt_entry_t;

typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  reserved[48];
    ahci_prdt_entry_t prdt[1];
} __attribute__((packed)) ahci_cmd_table_t;

#define FIS_TYPE_REG_H2D    0x27
#define FIS_TYPE_REG_D2H    0x34
#define FIS_TYPE_DMA_ACT    0x39
#define FIS_TYPE_DMA_SETUP  0x41
#define FIS_TYPE_DATA       0x46
#define FIS_TYPE_BIST       0x58
#define FIS_TYPE_PIO_SETUP  0x5F
#define FIS_TYPE_DEV_BITS   0xA1

typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport:4;
    uint8_t  rsv0:3;
    uint8_t  c:1;
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  featureh;
    uint8_t  countl;
    uint8_t  counth;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv1[4];
} __attribute__((packed)) fis_reg_h2d_t;

#define HBA_PxCMD_ST        0x0001
#define HBA_PxCMD_FRE       0x0010
#define HBA_PxCMD_FR        0x4000
#define HBA_PxCMD_CR        0x8000

#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA_EXT   0x35
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_FLUSH_CACHE     0xE7

#define HBA_PORT_IPM_ACTIVE     1
#define HBA_PORT_DET_PRESENT    3

void ahci_init(void);
void ahci_scan_ports(void);
int ahci_read_sector(uint32_t lba, uint16_t* buf);
int ahci_write_sector(uint32_t lba, uint16_t* buf);
int ahci_identify_drive(void);
void find_all_controllers(void);
int eject_cdrom(int port);
int close_tray(int port);
int check_cdrom_status(int port);
int send_cmd(int port, uint8_t cmd, uint32_t lba, uint16_t* buffer);

#ifdef __cplusplus
}
#endif

#endif