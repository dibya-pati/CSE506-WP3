#ifndef __KPRINTF_H
#define __KPRINTF_H

#include <sys/defs.h>
#include <sys/tarfs.h>
#include <sys/ahci.h>
#include <sys/gdt.h>

void kprintf(const char *fmt, ...);
void flushtime(int seconds);
void flushLastKeyPress(char a, char b);

#endif
