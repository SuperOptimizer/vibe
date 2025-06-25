#pragma once


typedef enum rv_exception {
  RV_EIALIGN = 0,
  RV_EIFAULT = 1,
  RV_EILL = 2,
  RV_EBP = 3,
  RV_ELALIGN = 4,
  RV_ELFAULT = 5,
  RV_ESALIGN = 6,
  RV_ESFAULT = 7,
  RV_EUECALL = 8,
  RV_ESECALL = 9,
  RV_EMECALL = 11,
  RV_EIPAGE = 12,
  RV_ELPAGE = 13,
  RV_ESPAGE = 15
} rv_exception;

/* Result type: one of {RV_OK, RV_BAD, RV_PAGEFAULT, RV_BAD_ALIGN} */
typedef enum rv_res {
  RV_OK = 0,
  RV_BAD = 1,
  RV_BAD_ALIGN = 2,
  RV_PAGEFAULT = 3,
  RV_TRAP_NONE = 0x80000010,
  RV_TRAP_WFI = 0x80000011
} rv_res;

typedef enum rv_priv { RV_PUSER = 0, RV_PSUPER = 1, RV_PMACH = 3 } rv_priv;
typedef enum rv_access { RV_AR = 1, RV_AW = 2, RV_AX = 4 } rv_access;
typedef enum rv_cause { RV_CSI = 8, RV_CTI = 128, RV_CEI = 512 } rv_cause;

typedef struct rv_csr {
  u32 /* sstatus, */ sie, stvec, scounteren, sscratch, sepc, scause, stval,
      sip, satp;
  u32 mstatus, misa, medeleg, mideleg, mie, mtvec, mcounteren, mstatush,
      mscratch, mepc, mcause, mtval, mip, mtime, mtimeh, mvendorid, marchid,
      mimpid, mhartid;
  u32 cycle, cycleh;
} rv_csr;


#define RV_TLB_ENTRIES 32
#define RV_TLB_SETS 8
#define RV_TLB_WAYS 4
#define RV_TLB_SET_MASK (RV_TLB_SETS - 1)

typedef struct rv_tlb_entry {
  u32 va;   /* virtual address (page aligned) */
  u32 pte;  /* page table entry */
  u8 valid; /* valid bit */
  u8 level; /* page table level (0 or 1 for SV32) */
  u8 asid;  /* address space ID (from SATP) */
  u8 lru;   /* LRU counter for replacement */
} rv_tlb_entry;

struct rv {
  mach *mach;
  u32 r[32];                        /* registers */
  u32 pc;                           /* program counter */
  u32 next_pc;                      /* program counter for next cycle */
  rv_csr csr;                       /* csr state */
  u32 priv;                         /* current privilege level*/
  u32 res, res_valid;               /* lr/sc reservation set */
  rv_tlb_entry tlb[RV_TLB_SETS][RV_TLB_WAYS]; /* 8-way set associative TLB */
};

/* Initialize CPU. You can call this again on `cpu` to reset it. */
void rv_init(rv *cpu, void *user);

/* Single-step CPU. Returns trap cause if trap occurred, else `RV_TRAP_NONE` */
u32 rv_step(rv *cpu);
void rv_irq(rv *cpu, rv_cause cause);
void rv_endcvt(u8 *in, u8 *out, u32 width, bool is_store);

