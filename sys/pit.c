#include<sys/kprintf.h>


void init_pit()
{

unsigned int counter=59659;

unsigned int command=(unsigned int)0x34;

__asm__(
"mov %0, %%al;\n"
"outb %%al, $0x43;\n"
"mov %1,%%ax;\n"
"outb %%al,$0x40;\n"
"mov %%ah, %%al;\n"
"outb %%al,$0x40;\n"
:
:"m"(command),"m"(counter),"K"(0x43),"K"(0x40)
);

}


void read_pit()
{
unsigned int counter;
unsigned int command=(unsigned int)0x0;
__asm__(
"mov %1,%%al;\n"
"outb %%al,$0x43;\n"
"inb $0x40,%%al;\n"
"mov %%al,%%ah;\n"
"inb $0x40,%%al;\n"
"rol $8,%%ax;\n"
"mov %%ax,%0;\n"
:"=m"(counter)
:"m"(command)
);
kprintf("counter value:%d\n",counter);

}
