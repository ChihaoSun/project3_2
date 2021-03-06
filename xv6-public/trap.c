#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

pte_t* swapout(void);

void dumppte(struct proc* pte){
  cprintf("%d %d %d\n",pte->pagenum, pte->phy_pagenum,pte->sz);

}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }
  if(tf->trapno == T_PGFLT) {
	cprintf("pagefault++++++++++++++++++++++++++\n");
	uint va=rcr2();
	myproc()->pagefault++;
        //dumppte(myproc());
	uint needswapin=0;
        uint swapinloc=0;
	char *swapoutbuf=kalloc();
       if(myproc()->pagenum>=MAX_TOTAL_PAGES){
	   kill(myproc()->pid);
	   //cprintf("here kill %d\n",myproc()->pid);
	   return ;
        }
        pte_t* pte;
        pte=walkpgdir(myproc()->pgdir,(void *)va,0);
	//char swapinbuf1[PGSIZE/2];
	char * swapinbuf = kalloc();
        if(!((*pte)&PTE_PG)){
            myproc()->pagenum++;
        }else{
	   //store swap in content in buffer first
	   needswapin=1;
	   cprintf("want to swap in\n");
	   swapinloc=(PTE_ADDR(*pte)>>12)&0xFFFFF;
	   cprintf("read offset%x\n",swapinloc);
           readFromSwapFile(myproc(),swapinbuf,swapinloc,PGSIZE);
	}
	//dumppte(myproc());
        if(myproc()->phy_pagenum<MAX_PSYC_PAGES){
	    //cprintf("here\n");
            char * mem;
            mem = kalloc();
	    myproc()->phy_pagenum++;
	    //cprintf("%x\n",mem);
	    //cprintf("%x\n",va);
            if(mem == 0){
                cprintf("alloc lazy page, out of memory\n");
              return ;
            }
            memset(mem, 0, PGSIZE);
	   //cprintf("newpage\n");

            uint a = PGROUNDDOWN(va);
	    //cprintf("%x	%x\n",a,V2P(mem));
	    // cprintf("%s %d %d\n",myproc()->name,myproc()->sz,myproc()->pid);
            mappages(myproc()->pgdir, (char*) a, PGSIZE, V2P(mem), PTE_W | PTE_U);
	    //uint z=*pte;
	    //cprintf("%x\n",z);
	}
	
	 else{
           //choose a victim to swap out
           
	   
	   cprintf("swapout faultedpt %x\n",pte);
           myproc()->totalpageout++;
	    pte_t* outpage=swapout();
	    uint outaddr=*outpage;
	    cprintf("swqpoupte %x\n",outpage);
	    cprintf("swqpouptebefore %x\n",*outpage);
	    cprintf("swqpouloc %x\n",myproc()->swaploc);
	    if(!needswapin){
	    writeToSwapFile(myproc(),(char *)V2P(PTE_ADDR(*outpage)),myproc()->swaploc,PGSIZE);
	    *outpage=PTE_FLAGS(*outpage)|(myproc()->swaploc<<12);
	    cprintf("swqpoupteafter %x\n",outpage);
	    myproc()->swaploc+=PGSIZE;
	    }else{
		cprintf("sepcial swapout to %x\n",swapinloc);
		memmove(swapoutbuf,(char *)V2P(PTE_ADDR(*outpage)),PGSIZE);
		writeToSwapFile(myproc(),swapoutbuf,swapinloc,PGSIZE);
		*outpage=PTE_FLAGS(*outpage)|(swapinloc<<12);
 	    }
	   //update flag
	   *outpage&=~PTE_P;
	   *outpage|=PTE_PG;
	   cprintf("swqpoupteafter %x\n",*outpage);
	   cprintf("swapout\n");
	   uint a = PGROUNDDOWN(va);
           mappages(myproc()->pgdir, (char*) a, PGSIZE, (PTE_ADDR(outaddr)), PTE_W | PTE_U);
	   //cprintf("mappage success\n");
        }
	
        //check for swapin
        //cprintf("%x\n",PTE_ADDR(*pte));
        //pte=walkpgdir(myproc()->pgdir,(void *)va,0);
        if(!needswapin){
	//no need for swap in, clear contents
	  // cprintf("NOTSWAPIN\n");
            memset((void *)P2V((PTE_ADDR(*pte))),0,PGSIZE);
        }else{
	//swap in contents
	   cprintf("SWAPIN");
           char * swapindest=(char *)PTE_ADDR(*pte);
	   memmove(P2V(swapindest),swapinbuf,PGSIZE);
	   //writeToSwapFile(myproc(),swapoutbuf,myproc()->swaploc,PGSIZE);

	   kfree(swapinbuf);
        }
	cprintf("pageoutsolve-----------------------\n\n");
	return ;
	

  }
 
  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
unsigned long randstate = 1;
unsigned int
rand()
{
  randstate = randstate * 1664525 + 1013904223;
  return randstate;
}


pte_t* swapout(){
	//#ifdef RAND
	struct proc* p = myproc();
        pte_t *pte;
        pte_t *ptes[15];
        int i,j=0,total=0;
        for(i=0; i<p->sz; i+=PGSIZE){
        	pte = walkpgdir(p->pgdir, (char *)i, 0);
        	if(*pte & PTE_P){ //check PTE_P
            		total++;
            		ptes[j++]=pte;
        	}
       }   
       return ptes[rand()%total];
	//#endif	
}
