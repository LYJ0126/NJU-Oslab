#include "common.h"
#include "x86.h"
#include "device.h"

void kEntry(void) {
	// Interruption is disabled in bootloader
	initSerial();// initialize serial port
	
	// TODO: 做一系列初始化
	// initialize idt
	initIdt();
	// initialize 8259a
	initIntr();
	// initialize gdt, tss
	initSeg();
	// initialize vga device
	initVga();
	// initialize keyboard device
	initKeyTable();
	//检测串口
	//putChar('H');
	//putChar('e');
	//putChar('l');
	//putChar('l');
	//putChar('o');
	//putChar('!');
	//putChar('\n');


	loadUMain(); // load user program, enter user space
	//putChar('#');
	while(1);
	assert(0);
}
