void checkAllBuses(void);
void GetABAR(uint8_t bus, uint8_t device);
uint32_t pciConfigReadLong (uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint32_t pciConfigReadBAR (uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);