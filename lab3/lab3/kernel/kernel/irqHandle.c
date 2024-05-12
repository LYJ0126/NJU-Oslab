#include "x86.h"
#include "device.h"

extern TSS tss;
extern ProcessTable pcb[MAX_PCB_NUM];
extern int current;

extern int displayRow;
extern int displayCol;

void GProtectFaultHandle(struct StackFrame *sf);

void syscallHandle(struct StackFrame *sf);

void syscallWrite(struct StackFrame *sf);
void syscallPrint(struct StackFrame *sf);

void timerHandle(struct StackFrame *sf);
void syscallFork(struct StackFrame* sf);
void syscallSleep(struct  StackFrame* sf);
void syscallExit(struct StackFrame* sf);

void irqHandle(struct StackFrame *sf)
{ // pointer sf = esp
	/* Reassign segment register */
	asm volatile("movw %%ax, %%ds" ::"a"(KSEL(SEG_KDATA)));
	/*XXX Save esp to stackTop */
	uint32_t tmpStackTop = pcb[current].stackTop;
	pcb[current].prevStackTop = pcb[current].stackTop;
	pcb[current].stackTop = (uint32_t)sf;

	switch (sf->irq)
	{
	case -1:
		break;
	case 0xd:
		GProtectFaultHandle(sf);
		break;
	case 0x20:
		timerHandle(sf);
		break;
	case 0x80:
		syscallHandle(sf);
		break;
	default:
		assert(0);
	}
	/*XXX Recover stackTop */
	pcb[current].stackTop = tmpStackTop;
}

void GProtectFaultHandle(struct StackFrame *sf)
{
	assert(0);
	return;
}

void timerHandle(struct StackFrame *sf)
{
	// TODO
	int i = (current + 1) % MAX_PCB_NUM;
	for(; i != current; i = (i + 1) % MAX_PCB_NUM){
		if(pcb[i].state == STATE_BLOCKED){//sleep状态
			if(pcb[i].sleepTime > 0){
				pcb[i].sleepTime--;
				if(pcb[i].sleepTime == 0) pcb[i].state = STATE_RUNNABLE;
			}
		}
	}
	if(pcb[current].state == STATE_RUNNING && pcb[current].timeCount < MAX_TIME_COUNT){
		pcb[current].timeCount++;
		return;
	}
	if(pcb[current].state == STATE_RUNNING && pcb[current].timeCount >= MAX_TIME_COUNT){
		pcb[current].timeCount = 0;
		pcb[current].state = STATE_RUNNABLE;
	}
	for(i = (current + 1) % MAX_PCB_NUM; i != current; i = (i + 1) % MAX_PCB_NUM){
		if(pcb[i].state == STATE_RUNNABLE && i!=0){
			current = i;
			break;
		}
	}
	if(pcb[i].state != STATE_RUNNABLE) current = 0;
	pcb[current].state = STATE_RUNNING;
	pcb[current].timeCount = 0;
	uint32_t tmpStackTop = pcb[current].stackTop;
	pcb[current].stackTop = pcb[current].prevStackTop;
	tss.esp0 = (uint32_t)&(pcb[current].stackTop);
	asm volatile("movl %0, %%esp"::"m"(tmpStackTop)); // switch kernel stack
	asm volatile("popl %gs");
	asm volatile("popl %fs");
	asm volatile("popl %es");
	asm volatile("popl %ds");
	asm volatile("popal");
	asm volatile("addl $8, %esp");
	asm volatile("iret");
}

void syscallHandle(struct StackFrame *sf)
{
	switch (sf->eax)
	{ // syscall number
	case 0:
		syscallWrite(sf);
		break; // for SYS_WRITE
	/*TODO Add Fork,Sleep... */
	case 1:
		syscallFork(sf);
		break;
	case 3:
		syscallSleep(sf);
		break;
	case 4:
		syscallExit(sf);
		break;
	default:
		break;
	}
}

void syscallWrite(struct StackFrame *sf)
{
	switch (sf->ecx)
	{ // file descriptor
	case 0:
		syscallPrint(sf);
		break; // for STD_OUT
	default:
		break;
	}
}

void syscallPrint(struct StackFrame *sf)
{
	int sel = sf->ds; // segment selector for user data, need further modification
	char *str = (char *)sf->edx;
	int size = sf->ebx;
	int i = 0;
	int pos = 0;
	char character = 0;
	uint16_t data = 0;
	asm volatile("movw %0, %%es" ::"m"(sel));
	for (i = 0; i < size; i++)
	{
		asm volatile("movb %%es:(%1), %0" : "=r"(character) : "r"(str + i));
		if (character == '\n')
		{
			displayRow++;
			displayCol = 0;
			if (displayRow == 25)
			{
				displayRow = 24;
				displayCol = 0;
				scrollScreen();
			}
		}
		else
		{
			data = character | (0x0c << 8);
			pos = (80 * displayRow + displayCol) * 2;
			asm volatile("movw %0, (%1)" ::"r"(data), "r"(pos + 0xb8000));
			displayCol++;
			if (displayCol == 80)
			{
				displayRow++;
				displayCol = 0;
				if (displayRow == 25)
				{
					displayRow = 24;
					displayCol = 0;
					scrollScreen();
				}
			}
		}
		// asm volatile("int $0x20"); //XXX Testing irqTimer during syscall
		// asm volatile("int $0x20":::"memory"); //XXX Testing irqTimer during syscall
	}

	updateCursor(displayRow, displayCol);
	// take care of return value
	return;
}

// TODO syscallFork ...
void syscallFork(struct StackFrame* sf){
	//查找空闲pcb
	int pos = -1;
	int j;
	for(j = 0; j < MAX_PCB_NUM; ++j){
		if(pcb[j].state == STATE_DEAD){
			pos = j;
			break;
		}
	}
	if(pos == -1){//没有空闲pcb，父进程返回-1,
		//sf->eax = -1;
		pcb[current].regs.eax = -1;
		return;
	}
	//资源复制
	//memcpy((void* )((pos + 1) * 0x100000), (void* )((current + 1) * 0x100000), 0x100000);
	enableInterrupt();//开中断
	int i;
	for(i = 0; i < 0x100000; ++i){
		*(unsigned char *)(i + (pos + 1) * 0x100000) = *(unsigned char *)(i + (current + 1) * 0x100000);
	}
	disableInterrupt();//关中断
	pcb[pos].stackTop = pcb[current].stackTop + (uint32_t)(&pcb[pos].stackTop) - (uint32_t)(&pcb[current].stackTop);
	pcb[pos].prevStackTop = pcb[current].prevStackTop + (uint32_t)(&pcb[pos].stackTop) - (uint32_t)(&pcb[current].stackTop);
	pcb[pos].regs.edi = pcb[current].regs.edi;
	pcb[pos].regs.esi = pcb[current].regs.esi;
	pcb[pos].regs.ebp = pcb[current].regs.ebp;
	pcb[pos].regs.xxx = pcb[current].regs.xxx;
	pcb[pos].regs.ebx = pcb[current].regs.ebx;
	pcb[pos].regs.edx = pcb[current].regs.edx;
	pcb[pos].regs.ecx = pcb[current].regs.ecx;
	pcb[pos].regs.eax = pcb[current].regs.eax;
	pcb[pos].regs.irq = pcb[current].regs.irq;
	pcb[pos].regs.error = pcb[current].regs.error;
	pcb[pos].regs.eip = pcb[current].regs.eip;
	pcb[pos].regs.eflags = pcb[current].regs.eflags;
	pcb[pos].regs.esp = pcb[current].regs.esp;

	pcb[pos].regs.cs = USEL(1 + 2 * pos);
	pcb[pos].regs.ss = USEL(2 + 2 * pos);
	pcb[pos].regs.ds = USEL(2 + 2 * pos);
	pcb[pos].regs.es = USEL(2 + 2 * pos);
	pcb[pos].regs.fs = USEL(2 + 2 * pos);
	pcb[pos].regs.gs = USEL(2 + 2 * pos);
	pcb[pos].state = STATE_RUNNABLE;
	pcb[pos].timeCount = pcb[current].timeCount;
	pcb[pos].sleepTime = pcb[current].sleepTime;
	pcb[pos].pid = pos;
	pcb[pos].regs.eax = 0;//子进程返回0
	pcb[current].regs.eax = pos;//父进程返回pid
}

void syscallSleep(struct  StackFrame* sf)
{
	if(sf->ecx < 0) assert(0);//时间不能小于0
	if(sf->ecx == 0) return;
	pcb[current].sleepTime = sf->ecx;
	pcb[current].state = STATE_BLOCKED;//阻塞
	asm volatile("int $0x20");
}

void syscallExit(struct StackFrame* sf)
{
	pcb[current].state = STATE_DEAD;
	asm volatile("int $0x20");
}