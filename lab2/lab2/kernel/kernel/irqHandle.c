#include "x86.h"
#include "device.h"

extern int displayRow;
extern int displayCol;

extern uint32_t keyBuffer[MAX_KEYBUFFER_SIZE];
extern int bufferHead;
extern int bufferTail;

int tail=0;

void GProtectFaultHandle(struct TrapFrame *tf);

void KeyboardHandle(struct TrapFrame *tf);

void syscallHandle(struct TrapFrame *tf);
void syscallWrite(struct TrapFrame *tf);
void syscallPrint(struct TrapFrame *tf);
void syscallRead(struct TrapFrame *tf);
void syscallGetChar(struct TrapFrame *tf);
void syscallGetStr(struct TrapFrame *tf);


void irqHandle(struct TrapFrame *tf) { // pointer tf = esp
	/*
	 * 中断处理程序
	 */
	/* Reassign segment register */
	asm volatile("movw %%ax, %%ds"::"a"(KSEL(SEG_KDATA)));

	switch(tf->irq) {
		// TODO: 填好中断处理程序的调用
		case -1: break;//目前不处理
		case 0xd: 
			GProtectFaultHandle(tf);
			break;
		case 0x21:
			//putChar('k');
			KeyboardHandle(tf);
			break;
		case 0x80:
			//putChar('s');
			syscallHandle(tf);
			break;
		default:assert(0);
	}
}

void GProtectFaultHandle(struct TrapFrame *tf){
	assert(0);
	return;
}

void KeyboardHandle(struct TrapFrame *tf){
	uint32_t code = getKeyCode();

	if(code == 0xe){ // 退格符
		//要求只能退格用户键盘输入的字符串，且最多退到当行行首
		if(displayCol>0&&displayCol>tail){
			displayCol--;
			uint16_t data = 0 | (0x0c << 8);
			int pos = (80*displayRow+displayCol)*2;
			asm volatile("movw %0, (%1)"::"r"(data),"r"(pos+0xb8000));
		}
	}else if(code == 0x1c){ // 回车符
		//处理回车情况
		keyBuffer[bufferTail++]='\n';
		displayRow++;
		displayCol=0;
		tail=0;
		if(displayRow==25){
			scrollScreen();
			displayRow=24;
			displayCol=0;
		}
	}else if(code < 0x81){
		// TODO: 处理正常的字符
		char letter = getChar(code);
		if(letter){//code < KF12_P
			putChar(letter);
			uint16_t data = letter | (0x0c << 8);
			keyBuffer[bufferTail++] = letter;
			bufferTail %= MAX_KEYBUFFER_SIZE;
			int pos = (80 * displayRow+displayCol) * 2;
			asm volatile("movw %0, (%1)"::"r"(data),"r"(pos+0xb8000));
			displayCol++;
			if(displayCol == 80){//行已满
				displayCol = 0;
				displayRow++;
				if(displayRow==25){//屏幕已满
					displayRow=24;
					displayCol=0;
					scrollScreen();
				}
			}
		}

	}
	updateCursor(displayRow, displayCol);//更新光标
	
}

void syscallHandle(struct TrapFrame *tf) {
	//putChar('y');
	switch(tf->eax) { // syscall number
		case 0:
			syscallWrite(tf);
			break; // for SYS_WRITE
		case 1:
			syscallRead(tf);
			break; // for SYS_READ
		default:break;
	}
}

void syscallWrite(struct TrapFrame *tf) {
	//putChar('y');
	switch(tf->ecx) { // file descriptor
		case 0:
			syscallPrint(tf);
			break; // for STD_OUT
		default:break;
	}
}

void syscallPrint(struct TrapFrame *tf) {
	//putChar('y');
	int sel =  USEL(SEG_UDATA);
	char *str = (char*)tf->edx;
	int size = tf->ebx;
	int i = 0;
	int pos = 0;
	char character = 0;
	uint16_t data = 0;
	asm volatile("movw %0, %%es"::"m"(sel));
	for (i = 0; i < size; i++) {
		asm volatile("movb %%es:(%1), %0":"=r"(character):"r"(str+i));
		// TODO: 完成光标的维护和打印到显存
		if(character == '\n'){//换行
			displayCol = 0;
			displayRow++;
			if(displayRow == 25){//屏幕已满
				displayRow=24;
				displayCol=0;
				scrollScreen();
			}
		}
		else{
			data = character | (0x0c << 8);
			pos = (80 * displayRow+displayCol) * 2;
			asm volatile("movw %0, (%1)"::"r"(data),"r"(pos+0xb8000));//显示在屏幕displayRow行displayCol列
			displayCol++;
			if(displayCol == 80){//行已满
				displayCol = 0;
				displayRow++;
				if(displayRow == 25){//屏幕已满
					displayRow=24;
					displayCol=0;
					scrollScreen();
				}
			}
		}

	}
	tail=displayCol;
	updateCursor(displayRow, displayCol);
}

void syscallRead(struct TrapFrame *tf){
	switch(tf->ecx){ //file descriptor
		case 0:
			syscallGetChar(tf);
			break; // for STD_IN
		case 1:
			syscallGetStr(tf);
			break; // for STD_STR
		default:break;
	}
}

void syscallGetChar(struct TrapFrame *tf){
	// TODO: 自由实现
	if(bufferTail < 1) tf->eax = 0;
	else if(keyBuffer[bufferTail - 1] == '\n'){
		while (bufferTail > bufferHead && keyBuffer[bufferTail-1] == '\n') keyBuffer[--bufferTail] = '\0';//多次回车
		tf->eax = keyBuffer[bufferHead];
		bufferHead++;
	}
	else tf->eax = 0;
}

void syscallGetStr(struct TrapFrame *tf){
	// TODO: 自由实现
	int len = tf->ebx;
	//char* str = (char*)(tf->edx);
	int flag = 0;
	int sel = USEL(SEG_UDATA);
	asm volatile("movw %0, %%es"::"m"(sel));
	if (keyBuffer[bufferTail - 1] == '\n')  flag = 1;
	while (bufferTail > bufferHead && keyBuffer[bufferTail-1] == '\n') keyBuffer[--bufferTail] = '\0';//多次回车
	if (flag == 0 && bufferTail - bufferHead < len) tf->eax = 0;
	else {
		int i=0;
		int temp = (len < bufferTail - bufferHead) ? len : bufferTail - bufferHead;
		while ( i < temp) {
			asm volatile("movb %1, %%es:(%0)"::"r"(tf->edx + i), "r"(keyBuffer[bufferHead+i]));
			//asm volatile("movb %0, %%es:(%1)"::"r"(keyBuffer[bufferHead + i]),"r"(tf->edx + i));
			i++;
		}
		tf->eax = 1;
		bufferHead=bufferTail;
	}
}
