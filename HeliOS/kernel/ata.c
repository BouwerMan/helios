// https://wiki.osdev.org/IDE#Detecting_IDE_Drives
#include <kernel/asm.h>
#include <kernel/ata.h>
#include <kernel/timer.h>
#include <stdio.h>

struct IDEChannelRegisters {
    unsigned short base;  // I/O Base.
    unsigned short ctrl;  // Control Base
    unsigned short bmide; // Bus Master IDE
    unsigned char nIEN;   // nIEN (No Interrupt);
} channels[2];

// NOTE: I changed the order of some of the static thingys idk if that broke anything
uint8_t ide_buf[2048] = { 0 };               // Buffer to read id space into
volatile static uint8_t ide_irq_invoked = 0; // if an irq is invoked
static uint8_t atapi_packet[12] = { 0xA8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // ATAPI Drives

struct ide_device {
    uint8_t reserved;          // 0 (Empty) or 1 (This Drive really exists).
    uint8_t channel;           // 0 (Primary Channel) or 1 (Secondary Channel).
    uint8_t drive;             // 0 (Master Drive) or 1 (Slave Drive).
    uint16_t type;             // 0: ATA, 1:ATAPI.
    uint16_t signature;        // Drive Signature
    uint16_t capabilities;     // Features.
    unsigned int command_sets; // Command Sets Supported.
    unsigned int size;         // Size in Sectors.
    uint8_t model[41];         // Model in string.
} ide_devices[4];

void ide_write(uint8_t channel, uint8_t reg, uint8_t data)
{
    if (reg > 0x07 && reg < 0x0C)
        ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);
    if (reg < 0x08)
        outb(channels[channel].base + reg - 0x00, data);
    else if (reg < 0x0C)
        outb(channels[channel].base + reg - 0x06, data);
    else if (reg < 0x0E)
        outb(channels[channel].ctrl + reg - 0x0A, data);
    else if (reg < 0x16)
        outb(channels[channel].bmide + reg - 0x0E, data);
    if (reg > 0x07 && reg < 0x0C) ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
}

uint8_t ide_read(uint8_t channel, uint8_t reg)
{
    unsigned char result;
    if (reg > 0x07 && reg < 0x0C)
        ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);
    if (reg < 0x08)
        result = inb(channels[channel].base + reg - 0x00);
    else if (reg < 0x0C)
        result = inb(channels[channel].base + reg - 0x06);
    else if (reg < 0x0E)
        result = inb(channels[channel].ctrl + reg - 0x0A);
    else if (reg < 0x16)
        result = inb(channels[channel].bmide + reg - 0x0E);
    if (reg > 0x07 && reg < 0x0C) ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
    return result;
}

// NOTE: Unsure what insl() is, moving on and i'll mess with it later
void ide_read_buffer(
    unsigned char channel, unsigned char reg, unsigned int buffer, unsigned int quads)
{
    /* WARNING: This code contains a serious bug. The inline assembly trashes ES and
     *           ESP for all of the code the compiler generates between the inline
     *           assembly blocks.
     */
    if (reg > 0x07 && reg < 0x0C)
        ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);
    asm("pushw %es; movw %ds, %ax; movw %ax, %es");
    if (reg < 0x08)
        insl(channels[channel].base + reg - 0x00, buffer, quads);
    else if (reg < 0x0C)
        insl(channels[channel].base + reg - 0x06, buffer, quads);
    else if (reg < 0x0E)
        insl(channels[channel].ctrl + reg - 0x0A, buffer, quads);
    else if (reg < 0x16)
        insl(channels[channel].bmide + reg - 0x0E, buffer, quads);
    asm("popw %es;");
    if (reg > 0x07 && reg < 0x0C) ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
}

uint8_t ide_polling(uint8_t channel, uint8_t advanced_check)
{
    // (I) Delay 400 nanosecond for BSY to be set:
    // -------------------------------------------------
    for (int i = 0; i < 4; i++)
        ide_read(channel,
            ATA_REG_ALTSTATUS); // Reading the Alternate Status port wastes 100ns; loop four times.

    // (II) Wait for BSY to be cleared:
    // -------------------------------------------------
    while (ide_read(channel, ATA_REG_STATUS) & ATA_SR_BSY)
        ; // Wait for BSY to be zero.

    if (advanced_check) {
        unsigned char state = ide_read(channel, ATA_REG_STATUS); // Read Status Register.

        // (III) Check For Errors:
        // -------------------------------------------------
        if (state & ATA_SR_ERR) return 2; // Error.

        // (IV) Check If Device fault:
        // -------------------------------------------------
        if (state & ATA_SR_DF) return 1; // Device Fault.

        // (V) Check DRQ:
        // -------------------------------------------------
        // BSY = 0; DF = 0; ERR = 0 so we should check for DRQ now.
        if ((state & ATA_SR_DRQ) == 0) return 3; // DRQ should be set
    }

    return 0; // No Error.
}

unsigned char ide_print_error(unsigned int drive, unsigned char err)
{
    if (err == 0) return err;

    printf("IDE:");
    if (err == 1) {
        printf("- Device Fault\n     ");
        err = 19;
    } else if (err == 2) {
        unsigned char st = ide_read(ide_devices[drive].channel, ATA_REG_ERROR);
        if (st & ATA_ER_AMNF) {
            printf("- No Address Mark Found\n     ");
            err = 7;
        }
        if (st & ATA_ER_TK0NF) {
            printf("- No Media or Media Error\n     ");
            err = 3;
        }
        if (st & ATA_ER_ABRT) {
            printf("- Command Aborted\n     ");
            err = 20;
        }
        if (st & ATA_ER_MCR) {
            printf("- No Media or Media Error\n     ");
            err = 3;
        }
        if (st & ATA_ER_IDNF) {
            printf("- ID mark not Found\n     ");
            err = 21;
        }
        if (st & ATA_ER_MC) {
            printf("- No Media or Media Error\n     ");
            err = 3;
        }
        if (st & ATA_ER_UNC) {
            printf("- Uncorrectable Data Error\n     ");
            err = 22;
        }
        if (st & ATA_ER_BBK) {
            printf("- Bad Sectors\n     ");
            err = 13;
        }
    } else if (err == 3) {
        printf("- Reads Nothing\n     ");
        err = 23;
    } else if (err == 4) {
        printf("- Write Protected\n     ");
        err = 8;
    }
    printf("- [%s %s] %s\n",
        (const char*[]) { "Primary",
            "Secondary" }[ide_devices[drive].channel], // Use the channel as an index into the array
        (const char*[]) {
            "Master", "Slave" }[ide_devices[drive].drive], // Same as above, using the drive
        ide_devices[drive].model);

    return err;
}

void ide_initialize(uint16_t BAR0, uint16_t BAR1, uint16_t BAR2, uint16_t BAR3, uint16_t BAR4)
{
    int i, j, k, count = 0;

    // 1- Detect I/O Ports which interface IDE Controller:
    channels[ATA_PRIMARY].base = (BAR0 & 0xFFFFFFFC) + 0x1F0 * (!BAR0);
    channels[ATA_PRIMARY].ctrl = (BAR1 & 0xFFFFFFFC) + 0x3F6 * (!BAR1);
    channels[ATA_SECONDARY].base = (BAR2 & 0xFFFFFFFC) + 0x170 * (!BAR2);
    channels[ATA_SECONDARY].ctrl = (BAR3 & 0xFFFFFFFC) + 0x376 * (!BAR3);
    channels[ATA_PRIMARY].bmide = (BAR4 & 0xFFFFFFFC) + 0;   // Bus Master IDE
    channels[ATA_SECONDARY].bmide = (BAR4 & 0xFFFFFFFC) + 8; // Bus Master IDE

    // 2- Disable IRQs:
    ide_write(ATA_PRIMARY, ATA_REG_CONTROL, 2);
    ide_write(ATA_SECONDARY, ATA_REG_CONTROL, 2);
    // 3- Detect ATA-ATAPI Devices:
    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++) {

            unsigned char err = 0, type = IDE_ATA, status;
            ide_devices[count].reserved = 0; // Assuming that no drive here.

            // (I) Select Drive:
            ide_write(i, ATA_REG_HDDEVSEL, 0xA0 | (j << 4)); // Select Drive.
            sleep(1);                                        // Wait 1ms for drive select to work.

            // (II) Send ATA Identify Command:
            ide_write(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
            sleep(1); // This function should be implemented in your OS. which waits for 1 ms.
                      // it is based on System Timer Device Driver.

            // (III) Polling:
            if (ide_read(i, ATA_REG_STATUS) == 0) continue; // If Status = 0, No Device.

            while (1) {
                status = ide_read(i, ATA_REG_STATUS);
                if ((status & ATA_SR_ERR)) {
                    err = 1;
                    break;
                } // If Err, Device is not ATA.
                if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break; // Everything is right.
            }

            // (IV) Probe for ATAPI Devices:

            if (err != 0) {
                unsigned char cl = ide_read(i, ATA_REG_LBA1);
                unsigned char ch = ide_read(i, ATA_REG_LBA2);

                if (cl == 0x14 && ch == 0xEB)
                    type = IDE_ATAPI;
                else if (cl == 0x69 && ch == 0x96)
                    type = IDE_ATAPI;
                else
                    continue; // Unknown Type (may not be a device).

                ide_write(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
                sleep(1);
            }

            // (V) Read Identification Space of the Device:
            ide_read_buffer(i, ATA_REG_DATA, (unsigned int)ide_buf, 128);

            // (VI) Read Device Parameters:
            ide_devices[count].reserved = 1;
            ide_devices[count].type = type;
            ide_devices[count].channel = i;
            ide_devices[count].drive = j;
            ide_devices[count].signature = *((unsigned short*)(ide_buf + ATA_IDENT_DEVICETYPE));
            ide_devices[count].capabilities
                = *((unsigned short*)(ide_buf + ATA_IDENT_CAPABILITIES));
            ide_devices[count].command_sets = *((unsigned int*)(ide_buf + ATA_IDENT_COMMANDSETS));

            // (VII) Get Size:
            if (ide_devices[count].command_sets & (1 << 26))
                // Device uses 48-Bit Addressing:
                ide_devices[count].size = *((unsigned int*)(ide_buf + ATA_IDENT_MAX_LBA_EXT));
            else
                // Device uses CHS or 28-bit Addressing:
                ide_devices[count].size = *((unsigned int*)(ide_buf + ATA_IDENT_MAX_LBA));

            // (VIII) String indicates model of device (like Western Digital HDD and SONY
            // DVD-RW...):
            for (k = 0; k < 40; k += 2) {
                ide_devices[count].model[k] = ide_buf[ATA_IDENT_MODEL + k + 1];
                ide_devices[count].model[k + 1] = ide_buf[ATA_IDENT_MODEL + k];
            }
            ide_devices[count].model[40] = 0; // Terminate String.

            count++;
        }

    // 4- Print Summary:
    for (i = 0; i < 4; i++)
        if (ide_devices[i].reserved == 1) {
            printf(" Found %s Drive %dGB - %s\n",
                (const char*[]) { "ATA", "ATAPI" }[ide_devices[i].type], /* Type */
                ide_devices[i].size / 1024 / 1024 / 2,                   /* Size */
                ide_devices[i].model);
        }
}
