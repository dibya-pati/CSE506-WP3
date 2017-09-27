#include <sys/defs.h>
#include <sys/ahci.h>
#include <sys/kprintf.h>
#include <sys/scanPCI.h>

#define	SATA_SIG_ATA	0x00000101	// SATA drive
#define	SATA_SIG_ATAPI	0xEB140101	// SATAPI drive
#define	SATA_SIG_SEMB	0xC33C0101	// Enclosure management bridge
#define	SATA_SIG_PM	0x96690101	// Port multiplier
 
#define AHCI_DEV_NULL   0x00000000


#define	AHCI_BASE	0x400000	// 4M
 
#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ 0x08
#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA_EX     0x35

// Start command engine
void start_cmd(hba_port_t *port)
{
	// Wait until CR (bit15) is cleared
	while (port->cmd & HBA_PxCMD_CR);
 
	// Set FRE (bit4) and ST (bit0)
	port->cmd |= HBA_PxCMD_FRE;
	port->cmd |= HBA_PxCMD_ST; 
}
 
// Stop command engine
void stop_cmd(hba_port_t *port)
{
	// Clear ST (bit0)
	port->cmd &= ~HBA_PxCMD_ST;
 
	// Wait until FR (bit14), CR (bit15) are cleared
	while(1)
	{
		if (port->cmd & HBA_PxCMD_FR)
			continue;
		if (port->cmd & HBA_PxCMD_CR)
			continue;
		break;
	}
 
	// Clear FRE (bit4)
	port->cmd &= ~HBA_PxCMD_FRE;
}

void memset(void* ptr, int data, int size){
	for (int i = 0; i < size; ++i)
	{
		*(unsigned char*)ptr = data;
		ptr++;
	}
}

void port_rebase(hba_port_t *port, int portno)
{
	stop_cmd(port);	// Stop command engine
 
	// Command list offset: 1K*portno
	// Command list entry size = 32
	// Command list entry maxim count = 32
	// Command list maxim size = 32*32 = 1K per port
	
	port->clb = AHCI_BASE + (portno<<10);
	// kprintf("rebase port %p\n", port->clb);
	// port->clbu = 0;
	memset((void*)(port->clb), 0, 1024);
 
	// FIS offset: 32K+256*portno
	// FIS entry size = 256 bytes per port
	port->fb = AHCI_BASE + (32<<10) + (portno<<8);
	// port->fbu = 0;
	memset((void*)(port->fb), 0, 256);
 
	// Command table offset: 40K + 8K*portno
	// Command table size = 256*32 = 8K per port
	hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)(port->clb);
	for (int i=0; i<32; i++)
	{
		cmdheader[i].prdtl = 8;	// 8 prdt entries per command table
					// 256 bytes per command table, 64+16+48+16*8
		// Command table offset: 40K + 8K*portno + cmdheader_index*256
		cmdheader[i].ctba = AHCI_BASE + (40<<10) + (portno<<13) + (i<<8);
		// cmdheader[i].ctbau = 0;
		memset((void*)cmdheader[i].ctba, 0, 256);
	}
 
	start_cmd(port);	// Start command engine
}

#define HBA_PORT_DET_PRESENT 0x03
#define HBA_PORT_IPM_ACTIVE 0x01
// Check device type
static int check_type(hba_port_t *port)
{
	uint32_t ssts = port->ssts;
	// kprintf("ssts : %p", ssts);
 
	uint8_t ipm = (ssts >> 8) & 0x0F;
	uint8_t det = ssts & 0x0F;
 
	// Read: https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1_3.pdf 
	// 3.3.10 Offset 28h: PxSSTS – Port x Serial ATA Status (SCR0: SStatus) 
	if (det != HBA_PORT_DET_PRESENT)	// Check drive status
		return AHCI_DEV_NULL;
	if (ipm != HBA_PORT_IPM_ACTIVE)
		return AHCI_DEV_NULL;
 
	switch (port->sig)
	{
	case SATA_SIG_ATAPI:
		return AHCI_DEV_SATAPI;
	case SATA_SIG_SEMB:
		return AHCI_DEV_SEMB;
	case SATA_SIG_PM:
		return AHCI_DEV_PM;
	default:
		return AHCI_DEV_SATA;
	}
}

// Find a free command list slot
int find_cmdslot2(hba_port_t *port)
{
	// If not set in SACT and CI, the slot is free
	uint32_t slots = (port->sact | port->ci);
	// kprintf("port->sact : %p", port->sact);

	for (int i=0; i<32; i++)
	{
		if ((slots&1) == 0)
			return i;
		slots >>= 1;
	}
	kprintf("Cannot find free command list entry\n");
	return -1;
}


int find_cmdslot(hba_mem_t *abar,hba_port_t *port)
{
	// If not set in SACT and CI, the slot is free
	uint32_t slots = (port->sact | port->ci);
        int cmdslots=(abar->cap & 0x0f00)>>8;
	for (int i=0; i<cmdslots; i++)
	{
		if ((slots&1) == 0)
			return i;
		slots >>= 1;
	}
	kprintf("Cannot find free command list entry\n");
	return -1;
} 

int diskwrite(hba_mem_t *abar,hba_port_t *port, uint32_t startl, uint32_t starth, uint32_t _count, uint64_t buf)
{
	uint32_t count = _count;
	port->is_rwc = 0xffff;		// Clear pending interrupt bits
	int spin = 0; // Spin lock timeout counter
	int slot = find_cmdslot(abar,port);
	if (slot == -1)
		return 0;
 
	hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)port->clb;
	cmdheader += slot;
	cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(uint32_t);	// Command FIS size
	cmdheader->w = 1;
		// Read from device
        cmdheader->c=1;
        cmdheader->p=1;
	cmdheader->prdtl = (uint16_t)((count-1)>>4) + 1;	// PRDT entries count
 
	hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)(cmdheader->ctba);
	memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t) +
 		(cmdheader->prdtl-1)*sizeof(hba_prdt_entry_t));
 
	// 8K bytes (16 sectors) per PRDT
        int i=0;
     
    // kprintf("cmdheader->prdtl-1 %d", cmdheader->prdtl); 
	for (i=0; i<cmdheader->prdtl-1; i++)
	{
		// kprintf("sec %d \n", i);
		cmdtbl->prdt_entry[i].dba = buf & 0xffffffffffffffff;
		cmdtbl->prdt_entry[i].dbc = 8*1024 - 1;	// 8K bytes
		cmdtbl->prdt_entry[i].i = 1;
		buf += 4*1024;	// 8K bytes
		count -= 16;	// 16 sectors
	}
	// Last entry
	cmdtbl->prdt_entry[i].dba = buf;
	cmdtbl->prdt_entry[i].dbc = (count)<<9;	// 512 bytes per sector
	cmdtbl->prdt_entry[i].i = 1;
 
	// Setup command
	fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);
 
	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1;	// Command
	cmdfis->command = ATA_CMD_WRITE_DMA_EX;
 
	cmdfis->lba0 = (uint8_t)startl&0x0000FF;

	cmdfis->lba1 = (uint8_t)(startl>>8)&0x0000FF;

	cmdfis->lba2 = (uint8_t)(startl>>16)&0x0000FF;
	cmdfis->device = 1<<6;	// LBA mode
 
	cmdfis->lba3 = (uint8_t)(startl>>24)&0x0000FF;
	cmdfis->lba4 = (uint8_t)starth&0x0000FF;
	cmdfis->lba5 = (uint8_t)(starth>>8)&0x0000FF;
 
	cmdfis->count = count;
 
	// The below loop waits until the port is no longer busy before issuing a new command
	while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000)
	{
		spin++;
	}
	if (spin == 1000000)
	{
		kprintf("Port is hung\n");
		return 0;
	}
 
	port->ci = 1<<slot;	// Issue command
 
	// Wait for completion
	while (1)
	{
		// In some longer duration reads, it may be helpful to spin on the DPS bit 
		// in the PxIS port field as well (1 << 5)
		if ((port->ci & (1<<slot)) == 0) 
			break;
		if (port->is_rwc & HBA_PxIS_TFES)	// Task file error
		{
			kprintf("Read disk error\n");
			return 0;
		}
	}
 
	// Check again
	if (port->is_rwc & HBA_PxIS_TFES)
	{
		kprintf("Read disk error\n");
		return 0;
	}
 
	return 1;




}




int diskread(hba_mem_t *abar,hba_port_t *port, uint32_t startl, uint32_t starth, uint32_t count, uint64_t buf)
{
        port->is_rwc = 0xffff;          // Clear pending interrupt bits
        int spin = 0; // Spin lock timeout counter
        int slot = find_cmdslot(abar,port);
        if (slot == -1)
                return 0;

        hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)port->clb;
        cmdheader += slot;
        cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(uint32_t);        // Command FIS size
        cmdheader->w = 0;               // Read from device
        cmdheader->c=1;
        cmdheader->p=1;
        cmdheader->prdtl = (uint16_t)((count-1)>>4) + 1;        // PRDT entries count

        hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)(cmdheader->ctba);
        memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t) +
             (cmdheader->prdtl-1)*sizeof(hba_prdt_entry_t));

        // 8K bytes (16 sectors) per PRDT
        int i=0;

        for (i=0; i<cmdheader->prdtl-1; i++)
        {
                cmdtbl->prdt_entry[i].dba = buf;
                cmdtbl->prdt_entry[i].dbc = 8*1024 - 1;     // 8K bytes
                cmdtbl->prdt_entry[i].i = 1;
                buf += 4*1024;    // 1K words
                count -= 16;    // 16 sectors
        }
        // Last entry
        cmdtbl->prdt_entry[i].dba = (uint64_t)buf;
        cmdtbl->prdt_entry[i].dbc = (count)<<9;   // 512 bytes per sector
        cmdtbl->prdt_entry[i].i = 1;

        // Setup command
        fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);

        cmdfis->fis_type = FIS_TYPE_REG_H2D;
        cmdfis->c = 1;  // Command
        cmdfis->command = ATA_CMD_READ_DMA_EX;

        cmdfis->lba0 = (uint8_t)startl;
        cmdfis->lba1 = (uint8_t)(startl>>8);
        cmdfis->lba2 = (uint8_t)(startl>>16);
        cmdfis->device = 1<<6;  // LBA mode

        cmdfis->lba3 = (uint8_t)(startl>>24);
        cmdfis->lba4 = (uint8_t)starth;
        cmdfis->lba5 = (uint8_t)(starth>>8);

        cmdfis->count = count & 0xffff;

        // The below loop waits until the port is no longer busy before issuing a new command
      while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000)
        {
                spin++;
        }
        if (spin == 1000000)
        {
                kprintf("Port is hung\n");
                return 0;
        }

        port->ci = 1<<slot;     // Issue command

        // Wait for completion
        while (1)
        {
                // In some longer duration reads, it may be helpful to spin on the DPS bit
                // in the PxIS port field as well (1 << 5)
                if ((port->ci & (1<<slot)) == 0)
                        break;
                if (port->is_rwc & HBA_PxIS_TFES)       // Task file error
                {
                        kprintf("Read disk error\n");
                        return 0;
                }
        }

        // Check again
        if (port->is_rwc & HBA_PxIS_TFES)
        {
                kprintf("Read disk error\n");
                return 0;
        }

        return 1;




}

int readFromDisk(hba_mem_t *abar, hba_port_t *port){
	int blocks = 100;
	int sectorSize = 512;		
	int sectorCount = 8;					
	int blockSize = sectorCount*sectorSize;			//8sectors in a block
	for (int block = 0; block < blocks; ++block)
	{
		char* buf = (char*)0x50c1000;

		int ret=diskread(abar,port,block*sectorCount,0,sectorCount,(uint64_t)buf);	
		if (ret != 1)
		{
			kprintf("DiskRead Failed for block%d\n", block);
			return ret;
		}

		kprintf("%d - %d ", buf[0], buf[blockSize-1]);		//Print first and last byte of each block to check write
	}
	return 1;
}

int writeToDisk(hba_mem_t *abar, hba_port_t *port){

	int blocks = 100;
	int sectorSize = 512;		
	int sectorCount = 8;					
	int blockSize = sectorCount*sectorSize;			//8sectors in a block
	for (int block = 0; block < blocks; ++block)
	{
		char* buf = (char*)0x40c1000;
		for (int i = 0; i < blockSize; ++i)
		{
			buf[i] = block;				//Write 0 in 0 block, 1 in 1 block, ...
		}

		int ret=diskwrite(abar,port,block*sectorCount,0,sectorCount,(uint64_t)buf);	
		if (ret != 1)
		{
			kprintf("DiskWrite Failed for block%d\n", block);
			return ret;
		}
	}

	return 1;
}


int once = 0;

void probe_port(hba_mem_t *abar)
{
	// Search disk in impelemented ports
	uint32_t pi = abar->pi;
	int i = 0;
	while (i<32)
	{
		if (pi & 1)
		{
			int dt = check_type(&abar->ports[i]);
			if (dt == AHCI_DEV_SATA)
			{
				if (!once)
				{

					pi >>= 1;
					i ++;
					once = 1;
					continue;
				}

				kprintf("SATA drive found at port %d\n", i);
				port_rebase(abar->ports, i);
				// kprintf("%p\n", abar->ports[i].sact);
				// kprintf("%p\n", abar->ports[i].ci);
				kprintf("Port Rebase done at port %d\n", i);

				writeToDisk(abar, &abar->ports[i]);
				readFromDisk(abar, &abar->ports[i]);

				break;											//Only do for first port
			}
			else if (dt == AHCI_DEV_SATAPI)
			{
				kprintf("SATAPI drive found at port %d\n", i);
			}
			else if (dt == AHCI_DEV_SEMB)
			{
				kprintf("SEMB drive found at port %d\n", i);
			}
			else if (dt == AHCI_DEV_PM)
			{
				kprintf("PM drive found at port %d\n", i);
			}
			else
			{
				kprintf("No drive found at port %d\n", i);
			}
		}
 
		pi >>= 1;
		i ++;
	}
}
 

void GetABAR(uint8_t bus, uint8_t device){
	uint8_t function = 0;
	uint8_t offset = 0x24;

	uint32_t abar = pciConfigReadBAR(bus, device, function, offset);
	kprintf("abar : %p\n", abar);
 	uint32_t hba_masked = abar & 0xFFFFFFF0;
	hba_mem_t* hba = (hba_mem_t*)(long)(hba_masked);
	probe_port(hba);
}