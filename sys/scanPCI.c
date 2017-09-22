#include <sys/Utils.h>
#include <sys/kprintf.h>

//Reference: http://wiki.osdev.org/PCI

uint32_t pciConfigReadLong (uint8_t bus, uint8_t slot,
                             uint8_t func, uint8_t offset)
 {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    // uint16_t tmp = 0;
 
    /* create configuration address as per Figure 1 */
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
 
    /* write out the address */
    sysOutLong (0xCF8, address);
    /* read in the data */
    // (offset & 2) * 8) = 0 will choose the first word of the 32 bits register 
    // tmp = (uint16_t)((sysInLong (0xCFC) >> ((offset & 2) * 8)) & 0xffff);
    uint32_t tmp = sysInLong (0xCFC);
    return (tmp);
 }


 void checkForAHCI(uint8_t bus, uint8_t device) {
    uint8_t function = 0;
 	uint8_t offset = 0x08;

     uint32_t row3 = pciConfigReadLong(bus, device, function, offset);
     if(row3 == 0xFFFFFFFF) return;        // Device doesn't exist

     uint8_t prog1F = (uint32_t)row3>>8 & (uint32_t)0xFF;
     uint8_t subclass = (uint32_t)row3>>16 & (uint32_t)0xFF;
     uint8_t classCode = (uint32_t)row3>>24 & (uint32_t)0xFF;

     if (classCode == (uint8_t)0x01 && subclass == (uint8_t)0x06)
     {
     	kprintf("AHCI prog1F: %p", prog1F);
     	offset = 0x00;
     	uint32_t row1 = pciConfigReadLong(bus, device, function, offset);
     	uint16_t vendorID = (uint32_t)row1 & (uint32_t)0xFFFF;
     	uint16_t deviceID = (uint32_t)row1>>16 & (uint32_t)0xFFFF;
     	kprintf("vendorID : %p deviceID : %p", vendorID, deviceID);
     }
 }

 void checkAllBuses(void) {
     uint16_t bus;
     uint8_t device;
 
     for(bus = 0; bus < 256; bus++) {
         for(device = 0; device < 32; device++) {
             checkForAHCI(bus, device);
         }
     }
 }