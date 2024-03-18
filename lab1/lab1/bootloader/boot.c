#include "boot.h"

#define SECTSIZE 512

void bootMain(void) {
	//TODO
	void (*app)(void) = (void(*)(void))0x8c00;//app函数指向0x8c00处(程序位置)
	readSect((void*)app, 1);//读取磁盘第1扇区中的Hello, World!程序至内存中0x9c00处(app/app.s中程序链接至此)
	app();//执行程序
}

void waitDisk(void) { // waiting for disk
	while((inByte(0x1F7) & 0xC0) != 0x40);
}

void readSect(void *dst, int offset) { // reading a sector of disk
	int i;
	waitDisk();
	outByte(0x1F2, 1);
	outByte(0x1F3, offset);
	outByte(0x1F4, offset >> 8);
	outByte(0x1F5, offset >> 16);
	outByte(0x1F6, (offset >> 24) | 0xE0);
	outByte(0x1F7, 0x20);

	waitDisk();
	for (i = 0; i < SECTSIZE / 4; i ++) {
		((int *)dst)[i] = inLong(0x1F0);
	}
}
