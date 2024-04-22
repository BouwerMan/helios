#include <kernel/asm.h>
#include <kernel/ata/controller.h>
#include <kernel/ata/device.h>
#include <kernel/pci/pci.h>
#include <stddef.h>
#include <stdio.h>

/* port-bases */
static const int PORTBASE_PRIMARY = 0x1F0;
static const int PORTBASE_SECONDARY = 0x170;

static const int IDE_CTRL_CLASS = 0x01;
static const int IDE_CTRL_SUBCLASS = 0x01;
static const int IDE_CTRL_BAR = 4;

static const pci_device_t* ide_ctrl;

static sATAController ctrls[2];

void ctrl_init()
{
    ide_ctrl = get_device_by_class(IDE_CTRL_CLASS, IDE_CTRL_SUBCLASS);
    if (!ide_ctrl) {
        puts("Could not get IDE controller.");
        return;
    }

    // maybe needed to check if I/O space is enabled?
    uint32_t status = pci_config_read_word(ide_ctrl->bus, ide_ctrl->dev, ide_ctrl->func, 0x04);
    if ((uint8_t)status == 0xFF) {
        puts("Floating IDE bus");
        return;
    } else {
        printf("IDE Status: 0x%X\n", status); // TODO: TEMP
    }

    ctrls[0].id = DEVICE_PRIMARY;
    ctrls[0].irq = CTRL_IRQ_BASE;
    ctrls[0].port_base = PORTBASE_PRIMARY;

    ctrls[1].id = DEVICE_SECONDARY;
    ctrls[1].irq = CTRL_IRQ_BASE + 1;
    ctrls[1].port_base = PORTBASE_SECONDARY;

    for (size_t i = 0; i < 2; i++) {
        printf("Initializing ctrl: %d\n", ctrls[i].id);

        ctrls[i].use_irq = false;
        ctrls[i].use_dma = false;

        // Init attached drives, beginning with slave
        for (short int j = 1; j >= 0; j--) {
            ctrls[i].devices[j].present = false;
            ctrls[i].devices[j].id = i * 2 + j;
            ctrls[i].devices[j].ctrl = ctrls + i;
            device_init(ctrls[i].devices + j);
        }
    }
}

void ctrl_outb(sATAController* ctrl, uint16_t reg, uint8_t value)
{
    outb(ctrl->port_base + reg, value);
}

uint8_t ctrl_inb(sATAController* ctrl, uint16_t reg) { return inb(ctrl->port_base + reg); }
uint16_t ctrl_inw(sATAController* ctrl, uint16_t reg) { return inw(ctrl->port_base + reg); }

void ctrl_inws(sATAController* ctrl, uint16_t reg, uint16_t* buff, size_t count)
{
    for (size_t i = 0; i < count; i++)
        buff[i] = inw(ctrl->port_base + reg);
}

void ctrl_outws(sATAController* ctrl, uint16_t reg, const uint16_t* buff, size_t count)
{
    for (size_t i = 0; i < count; i++)
        outword(ctrl->port_base + reg, buff[i]);
}

void ctrl_wait(sATAController* ctrl)
{
    inb(ctrl->port_base + ATA_REG_STATUS);
    inb(ctrl->port_base + ATA_REG_STATUS);
    inb(ctrl->port_base + ATA_REG_STATUS);
    inb(ctrl->port_base + ATA_REG_STATUS);
}
