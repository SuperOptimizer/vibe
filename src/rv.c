#include "vibe.h"

#define RV_RESET_VEC 0x80000000 /* CPU reset vector */

/* Convert bus_error to rv_res */
static inline rv_res bus_error_to_rv_res(bus_error err) {
  switch (err) {
  case BUS_OK:
    return RV_OK;
  case BUS_UNMAPPED:
  case BUS_INVALID:
    return RV_BAD;
  case BUS_ALIGN:
    return RV_BAD_ALIGN;
  default:
    return RV_BAD;
  }
}

#define rv_ext(c) (1 << (u8)((c) - 'A')) /* isa extension bit in misa */

void rv_init(rv *cpu, void *mach) {
  memset(cpu, 0, sizeof(*cpu));
  cpu->mach = mach;
  cpu->pc = RV_RESET_VEC;
  cpu->csr.misa = (1 << 30)     /* MXL = 1 [XLEN=32] */
                  | rv_ext('A') /* Atomics */
                  | rv_ext('C') /* Compressed Instructions */
                  | rv_ext('M') /* Multiplication and Division */
                  | rv_ext('S') /* Supervisor Mode */
                  | rv_ext('U') /* User Mode */;
  cpu->priv = RV_PMACH;

  /* Initialize instruction TLB */
  for (u32 s = 0; s < RV_ITLB_SETS; s++) {
    for (u32 w = 0; w < RV_ITLB_WAYS; w++) {
      cpu->itlb[s][w].valid = 0;
      cpu->itlb[s][w].lru = 0;
    }
  }

  /* Initialize data TLB */
  for (u32 s = 0; s < RV_DTLB_SETS; s++) {
    for (u32 w = 0; w < RV_DTLB_WAYS; w++) {
      cpu->dtlb[s][w].valid = 0;
      cpu->dtlb[s][w].lru = 0;
    }
  }

  /* Initialize superpage TLB */
  for (u32 i = 0; i < RV_STLB_ENTRIES; i++) {
    cpu->stlb[i].valid = 0;
    cpu->stlb[i].lru = 0;
  }

  cpu->current_asid = 0;
}

/* sign-extend x from h'th bit */
static u32 rv_signext(u32 x, u32 h) { return (0 - (x >> h)) << h | x; }

#define RV_SBIT   0x80000000               /* sign bit */
#define rv_sgn(x) (!!((u32)(x) & RV_SBIT)) /* extract sign */

/* compute overflow */
#define rv_ovf(a, b, y) ((((a) ^ (b)) & RV_SBIT) && (((y) ^ (a)) & RV_SBIT))

#define rv_bf(i, h, l)     (((i) >> (l)) & ((1 << ((h) - (l) + 1)) - 1)) /* extract bit field */
#define rv_b(i, l)         rv_bf(i, l, l)                                /* extract bit */
#define rv_tb(i, l, o)     (rv_b(i, l) << (o))                           /* translate bit */
#define rv_tbf(i, h, l, o) (rv_bf(i, h, l) << (o))                       /* translate bit field */

/* instruction field macros */
#define rv_ioph(i)    rv_bf(i, 6, 5)                   /* [h]i bits of opcode */
#define rv_iopl(i)    rv_bf(i, 4, 2)                   /* [l]o bits of opcode */
#define rv_if3(i)     rv_bf(i, 14, 12)                 /* funct3 */
#define rv_if5(i)     rv_bf(i, 31, 27)                 /* funct5 */
#define rv_if7(i)     rv_bf(i, 31, 25)                 /* funct7 */
#define rv_ird(i)     rv_bf(i, 11, 7)                  /* rd */
#define rv_irs1(i)    rv_bf(i, 19, 15)                 /* rs1 */
#define rv_irs2(i)    rv_bf(i, 24, 20)                 /* rs2 */
#define rv_iimm_i(i)  rv_signext(rv_bf(i, 31, 20), 11) /* imm. for I-type */
#define rv_iimm_iu(i) rv_bf(i, 31, 20)                 /* zero-ext'd. imm. for I-type */
#define rv_iimm_s(i)                                                                                                   \
  (rv_signext(rv_tbf(i, 31, 25, 5), 11) | rv_tbf(i, 30, 25, 5) | rv_bf(i, 11, 7)) /* imm. for S-type */
#define rv_iimm_u(i) rv_tbf(i, 31, 12, 12)                                        /* imm. for U-type */
#define rv_iimm_b(i)                                                                                                   \
  (rv_signext(rv_tb(i, 31, 12), 12) | rv_tb(i, 7, 11) | rv_tbf(i, 30, 25, 5) |                                         \
   rv_tbf(i, 11, 8, 1)) /* imm. for B-type */
#define rv_iimm_j(i)                                                                                                   \
  (rv_signext(rv_tb(i, 31, 20), 20) | rv_tbf(i, 19, 12, 12) | rv_tb(i, 20, 11) |                                       \
   rv_tbf(i, 30, 21, 1))                        /* imm. for J-type */
#define rv_isz(i) (rv_bf(i, 1, 0) == 3 ? 4 : 2) /* instruction size */

/* load register */
static u32 rv_lr(rv *cpu, u8 i) { return cpu->r[i]; }

/* store register */
static void rv_sr(rv *cpu, u8 i, u32 v) { cpu->r[i] = i ? v : 0; }

#define RV_CSR(num, r, w, dst) /* check if we are accessing csr `num` */                                               \
  y = ((csr == (num)) ? (rm = r, wm = w, &cpu->csr.dst) : y)

/* csr bus access -- we model csrs as an internal memory bus */
static rv_res rv_csr_bus(rv *cpu, u32 csr, u32 w, u32 *io) {
  u32 *y = NULL /* phys. register */, wm /* writable bits */ = -1U, rm = -1U;
  u32 rw = rv_bf(csr, 11, 10), priv = rv_bf(csr, 9, 8);
  if ((w && rw == 3) || cpu->priv < priv || (csr == 0x180 && cpu->priv == RV_PSUPER && rv_b(cpu->csr.mstatus, 20)))
    return RV_BAD; /* invalid access OR writing to satp with tvm=1 */
  /*     id     read mask   write mask  phys reg         csr name */
  RV_CSR(0x100, 0x800DE762, 0x800DE762, mstatus);    /*C sstatus */
  RV_CSR(0x104, 0x00000222, 0x00000222, mie);        /*C sie */
  RV_CSR(0x105, 0xFFFFFFFF, 0xFFFFFFFF, stvec);      /*C stvec */
  RV_CSR(0x106, 0xFFFFFFFF, 0x00000000, scounteren); /*C scounteren */
  RV_CSR(0x140, 0xFFFFFFFF, 0xFFFFFFFF, sscratch);   /*C sscratch */
  RV_CSR(0x141, 0xFFFFFFFF, 0xFFFFFFFF, sepc);       /*C sepc */
  RV_CSR(0x142, 0xFFFFFFFF, 0xFFFFFFFF, scause);     /*C scause */
  RV_CSR(0x143, 0xFFFFFFFF, 0xFFFFFFFF, stval);      /*C stval */
  RV_CSR(0x144, 0x00000222, 0x00000222, sip);        /*C sip */
  RV_CSR(0x180, 0xFFFFFFFF, 0xFFFFFFFF, satp);       /*C satp */
  RV_CSR(0x300, 0x807FFFEC, 0x807FFFEC, mstatus);    /*C mstatus */
  RV_CSR(0x301, 0xFFFFFFFF, 0x00000000, misa);       /*C misa */
  RV_CSR(0x302, 0xFFFFFFFF, 0xFFFFFFFF, medeleg);    /*C medeleg */
  RV_CSR(0x303, 0xFFFFFFFF, 0xFFFFFFFF, mideleg);    /*C mideleg */
  RV_CSR(0x304, 0xFFFFFFFF, 0x00000AAA, mie);        /*C mie */
  RV_CSR(0x305, 0xFFFFFFFF, 0xFFFFFFFF, mtvec);      /*C mtvec */
  RV_CSR(0x306, 0xFFFFFFFF, 0x00000000, mcounteren); /*C mcounteren */
  RV_CSR(0x310, 0x00000030, 0x00000030, mstatush);   /*C mstatush */
  RV_CSR(0x340, 0xFFFFFFFF, 0xFFFFFFFF, mscratch);   /*C mscratch */
  RV_CSR(0x341, 0xFFFFFFFF, 0xFFFFFFFF, mepc);       /*C mepc */
  RV_CSR(0x342, 0xFFFFFFFF, 0xFFFFFFFF, mcause);     /*C mcause */
  RV_CSR(0x343, 0xFFFFFFFF, 0x00000000, mtval);      /*C mtval */
  RV_CSR(0x344, 0xFFFFFFFF, 0x00000AAA, mip);        /*C mip */
  RV_CSR(0xC00, 0xFFFFFFFF, 0xFFFFFFFF, cycle);      /*C cycle */
  RV_CSR(0xC01, 0xFFFFFFFF, 0xFFFFFFFF, mtime);      /*C time */
  RV_CSR(0xC02, 0xFFFFFFFF, 0xFFFFFFFF, cycle);      /*C instret */
  RV_CSR(0xC80, 0xFFFFFFFF, 0xFFFFFFFF, cycleh);     /*C cycleh */
  RV_CSR(0xC81, 0xFFFFFFFF, 0xFFFFFFFF, mtimeh);     /*C timeh */
  RV_CSR(0xC82, 0xFFFFFFFF, 0xFFFFFFFF, cycleh);     /*C instreth */
  RV_CSR(0xF11, 0xFFFFFFFF, 0x00000000, mvendorid);  /*C mvendorid */
  RV_CSR(0xF12, 0xFFFFFFFF, 0x00000000, marchid);    /*C marchid */
  RV_CSR(0xF13, 0xFFFFFFFF, 0x00000000, mimpid);     /*C mimpid */
  RV_CSR(0xF14, 0xFFFFFFFF, 0xFFFFFFFF, mhartid);    /*C mhartid */
  if (!y) return RV_BAD;                             /* invalid csr */
  *io = w ? *io : (*y & rm);                         /* only read allowed bits */
  *y = w ? (*y & ~wm) | (*io & wm) : *y;             /* only write allowed bits  */

  /* Invalidate TLB when SATP is written */
  if (w && csr == 0x180) {
    /* Update cached ASID */
    cpu->current_asid = rv_bf(cpu->csr.satp, 30, 22);

    /* Invalidate all non-global entries in I-TLB */
    for (u32 s = 0; s < RV_ITLB_SETS; s++) {
      for (u32 w = 0; w < RV_ITLB_WAYS; w++) {
        if (!cpu->itlb[s][w].global) {
          cpu->itlb[s][w].valid = 0;
        }
      }
    }

    /* Invalidate all non-global entries in D-TLB */
    for (u32 s = 0; s < RV_DTLB_SETS; s++) {
      for (u32 w = 0; w < RV_DTLB_WAYS; w++) {
        if (!cpu->dtlb[s][w].global) {
          cpu->dtlb[s][w].valid = 0;
        }
      }
    }

    /* Invalidate all non-global entries in S-TLB */
    for (u32 i = 0; i < RV_STLB_ENTRIES; i++) {
      if (!cpu->stlb[i].global) {
        cpu->stlb[i].valid = 0;
      }
    }
  }

  return RV_OK;
}

/* trigger a trap */
static rv_exception rv_trap(rv *cpu, u32 cause, u32 tval) {
  u32 is_interrupt = !!(cause & 0x80000000), rcause = cause & ~0x80000000;
  rv_priv xp = /* destination privilege, switch from y = cpu->priv to this */
      (cpu->priv < RV_PMACH) && ((is_interrupt ? cpu->csr.mideleg : cpu->csr.medeleg) & (1 << rcause)) ? RV_PSUPER
                                                                                                       : RV_PMACH;
  u32 *xtvec = &cpu->csr.mtvec, *xepc = &cpu->csr.mepc, *xcause = &cpu->csr.mcause, *xtval = &cpu->csr.mtval;
  u32 xie = rv_b(cpu->csr.mstatus, xp);
  if (xp == RV_PSUPER) /* select s-mode regs */
    xtvec = &cpu->csr.stvec, xepc = &cpu->csr.sepc, xcause = &cpu->csr.scause, xtval = &cpu->csr.stval;
  cpu->csr.mstatus &= (xp == RV_PMACH ? 0xFFFFE777             /* {mpp, mie, mpie} <- 0 */
                                      : 0xFFFFFEDD);           /* {spp, sie, spie} <- 0 */
  cpu->csr.mstatus |= (cpu->priv << (xp == RV_PMACH ? 11 : 8)) /* xpp <- y */
                      | xie << (4 + xp);                       /* xpie <- xie */
  *xepc = cpu->pc;                                             /* xepc <- pc */
  *xcause = rcause | (is_interrupt << 31);                     /* xcause <- cause */
  *xtval = tval;                                               /* xtval <- tval */
  cpu->priv = xp;                                              /* priv <- x */
  /* if tvec[0], return 4 * cause + vec, otherwise just return vec */
  cpu->pc = (*xtvec & ~3U) + 4 * rcause * ((*xtvec & 1) && is_interrupt);
  return cause;
}

/* bus access trap with tval */
static u32 rv_trap_bus(rv *cpu, rv_exception err, u32 tval, rv_access a) {
  static const u32 ex[] = {RV_EIFAULT, RV_EIALIGN, RV_EIPAGE,  /* RV_AR */
                           RV_ELFAULT, RV_ELALIGN, RV_ELPAGE,  /* RV_AW */
                           RV_ESFAULT, RV_ESALIGN, RV_ESPAGE}; /* RV_AX */
  return rv_trap(cpu, ex[(a == RV_AW ? 2 : a == RV_AR) * 3 + err - 1], tval);
}

/* TLB lookup helper - check superpage TLB first */
static inline rv_tlb_entry *rv_tlb_lookup_stlb(rv *cpu, u32 va_page, u8 asid) {
  for (u32 i = 0; i < RV_STLB_ENTRIES; i++) {
    rv_tlb_entry *entry = &cpu->stlb[i];
    if (entry->valid && (entry->va & ~0x3FFFFF) == (va_page & ~0x3FFFFF) && (entry->global || entry->asid == asid)) {
      /* Update LRU */
      entry->lru = RV_STLB_ENTRIES - 1;
      for (u32 j = 0; j < RV_STLB_ENTRIES; j++) {
        if (j != i && cpu->stlb[j].lru > 0) cpu->stlb[j].lru--;
      }
      return entry;
    }
  }
  return NULL;
}

/* sv32 virtual address -> physical address */
static inline u32 rv_vmm(rv *cpu, u32 va, u32 *pa, rv_access access) {
  u32 epriv = rv_b(cpu->csr.mstatus, 17) && access != RV_AX ? rv_bf(cpu->csr.mstatus, 12, 11)
                                                            : cpu->priv; /* effective privilege mode */
  if (!rv_b(cpu->csr.satp, 31) || epriv > RV_PSUPER) {
    *pa = va; /* if !satp.mode, no translation */
    return RV_OK;
  }

  u32 va_page = va & ~0xFFFU;
  u8 asid = cpu->current_asid; /* Use cached ASID */
  rv_tlb_entry *tlb_entry = NULL;

  /* Check superpage TLB first (for 4MB pages) */
  tlb_entry = rv_tlb_lookup_stlb(cpu, va_page, asid);
  if (tlb_entry && tlb_entry->level == 1) {
    *pa = tlb_entry->pa | (va & 0x3FFFFF); /* Use pre-computed PA */
    goto permission_check;
  }

  /* Select appropriate TLB based on access type */
  rv_tlb_entry *tlb_base;
  u32 n_sets, n_ways, set_mask;
  if (access == RV_AX) {
    tlb_base = &cpu->itlb[0][0];
    n_sets = RV_ITLB_SETS;
    n_ways = RV_ITLB_WAYS;
    set_mask = RV_ITLB_SET_MASK;
  } else {
    tlb_base = &cpu->dtlb[0][0];
    n_sets = RV_DTLB_SETS;
    n_ways = RV_DTLB_WAYS;
    set_mask = RV_DTLB_SET_MASK;
  }

  /* Standard TLB lookup */
  u32 set = (va_page >> 12) & set_mask;
  for (u32 w = 0; w < n_ways; w++) {
    rv_tlb_entry *entry = tlb_base + (set * n_ways + w);
    if (entry->valid && entry->va == va_page && (entry->global || entry->asid == asid)) {
      tlb_entry = entry;
      /* Update LRU */
      entry->lru = n_ways - 1;
      for (u32 k = 0; k < n_ways; k++) {
        if (k != w && (tlb_base + (set * n_ways + k))->lru > 0) (tlb_base + (set * n_ways + k))->lru--;
      }
      *pa = entry->pa | (va & 0xFFF); /* Use pre-computed PA */
      goto permission_check;
    }
  }

  /* TLB miss - perform page table walk */
  u32 ppn = rv_bf(cpu->csr.satp, 21, 0);
  u32 a = ppn << 12, i = 1, pte, pte_address;

  while (1) {
    pte_address = a + (rv_bf(va, 21 + 10 * i, 12 + 10 * i) << 2);
    rv_res res = bus_error_to_rv_res(mach_bus(cpu->mach, pte_address, (u8 *)&pte, 0, 4));
    if (res != RV_OK) return res;
    rv_endcvt((u8 *)&pte, (u8 *)&pte, 4, 0);
    if (!rv_b(pte, 0) || (!rv_b(pte, 1) && rv_b(pte, 2))) return RV_PAGEFAULT;
    if (rv_b(pte, 1) || rv_b(pte, 3)) break; /* Leaf page */
    if (i == 0) return RV_PAGEFAULT;
    i = i - 1;
    a = rv_tbf(pte, 31, 10, 12);
  }

  /* Compute physical address */
  u32 computed_pa = rv_tbf(pte, 31, 10 + 10 * i, 12 + 10 * i) | rv_bf(va, 11 + 10 * i, 0);
  *pa = computed_pa;

  /* Insert into appropriate TLB */
  if (i == 1 && !rv_bf(pte, 19, 10)) { /* Valid 4MB superpage */
    /* Find LRU entry in STLB */
    u32 victim = 0;
    u8 min_lru = 255;
    for (u32 j = 0; j < RV_STLB_ENTRIES; j++) {
      if (!cpu->stlb[j].valid || cpu->stlb[j].lru < min_lru) {
        min_lru = cpu->stlb[j].lru;
        victim = j;
        if (!cpu->stlb[j].valid) break;
      }
    }
    tlb_entry = &cpu->stlb[victim];
    tlb_entry->pa = computed_pa & ~0x3FFFFF; /* Store base PA */
  } else {
    /* Regular page - find LRU entry in set */
    u32 victim = 0;
    u8 min_lru = 255;
    for (u32 w = 0; w < n_ways; w++) {
      rv_tlb_entry *e = tlb_base + (set * n_ways + w);
      if (!e->valid || e->lru < min_lru) {
        min_lru = e->lru;
        victim = w;
        if (!e->valid) break;
      }
    }
    tlb_entry = tlb_base + (set * n_ways + victim);
    tlb_entry->pa = computed_pa & ~0xFFF; /* Store base PA */
  }

  /* Fill TLB entry */
  tlb_entry->va = va_page;
  tlb_entry->pte = pte;
  tlb_entry->level = i;
  tlb_entry->asid = asid;
  tlb_entry->global = rv_b(pte, 5); /* G bit */
  tlb_entry->valid = 1;
  tlb_entry->lru = (i == 1 && tlb_entry == &cpu->stlb[0]) ? RV_STLB_ENTRIES - 1 : n_ways - 1;

permission_check:
  /* Fast permission check using cached PTE */
  pte = tlb_entry->pte;
  u32 mxr = rv_b(cpu->csr.mstatus, 19);
  u32 sum = rv_b(cpu->csr.mstatus, 18);
  u32 pte_u = rv_b(pte, 4);
  u32 pte_a = rv_b(pte, 6);
  u32 pte_d = rv_b(pte, 7);
  u32 pte_perms = rv_bf(pte, 3, 1);

  if (mxr) pte |= rv_b(pte, 3) << 2;
  if ((!pte_u && epriv == RV_PUSER) || (epriv == RV_PSUPER && !sum && pte_u) || (~pte_perms & access) || !pte_a ||
      ((access & RV_AW) && !pte_d))
    return RV_PAGEFAULT;

  return RV_OK;
}

#define rvm_lo(w) ((w) & (u32)0xFFFFU) /* low 16 bits of 32-bit word */
#define rvm_hi(w) ((w) >> 16)          /* high 16 bits of 32-bit word */

/* adc 16 bit */
static u32 rvm_ahh(u32 a, u32 b, u32 cin, u32 *cout) {
  u32 sum = a + b + cin /* cin must be less than 2. */;
  *cout = rvm_hi(sum);
  return rvm_lo(sum);
}

/* mul 16 bit */
static u32 rvm_mhh(u32 a, u32 b, u32 *cout) {
  u32 prod = a * b;
  *cout = rvm_hi(prod);
  return rvm_lo(prod);
}

/* 32 x 32 -> 64 bit multiply */
static u32 rvm(u32 a, u32 b, u32 *hi) {
  u32 al = rvm_lo(a), ah = rvm_hi(a), bl = rvm_lo(b), bh = rvm_hi(b);
  u32 qh, ql = rvm_mhh(al, bl, &qh);    /* qh, ql = al * bl      */
  u32 rh, rl = rvm_mhh(al, bh, &rh);    /* rh, rl = al * bh      */
  u32 sh, sl = rvm_mhh(ah, bl, &sh);    /* sh, sl = ah * bl      */
  u32 th, tl = rvm_mhh(ah, bh, &th);    /* th, tl = ah * bh      */
  u32 mc, m = rvm_ahh(rl, sl, 0, &mc);  /*  m, nc = rl + sl      */
  u32 nc, n = rvm_ahh(rh, sh, mc, &nc); /*  n, nc = rh + sh + nc */
  u32 x = ql;                           /*  x, 0  = ql           */
  u32 yc, y = rvm_ahh(m, qh, 0, &yc);   /*  y, yc = qh + m       */
  u32 zc, z = rvm_ahh(n, tl, yc, &zc);  /*  z, zc = tl + n  + yc */
  u32 wc, w = rvm_ahh(th, nc, zc, &wc); /*  w, 0  = th + nc + zc */
  *hi = z | (w << 16);                  /*   hi   = (w, z)       */
  return x | (y << 16);                 /*   lo   = (y, x)       */
}

#define rvc_op(c)      rv_bf(c, 1, 0)         /* c. op */
#define rvc_f3(c)      rv_bf(c, 15, 13)       /* c. funct3 */
#define rvc_rp(r)      ((r) + 8)              /* c. r' register offsetter */
#define rvc_ird(c)     rv_bf(c, 11, 7)        /* c. ci-format rd/rs1  */
#define rvc_irpl(c)    rvc_rp(rv_bf(c, 4, 2)) /* c. rd'/rs2' (bits 4-2) */
#define rvc_irph(c)    rvc_rp(rv_bf(c, 9, 7)) /* c. rd'/rs1' (bits 9-7) */
#define rvc_imm_ciw(c)                        /* CIW imm. for c.addi4spn */                                            \
  (rv_tbf(c, 10, 7, 6) | rv_tbf(c, 12, 11, 4) | rv_tb(c, 6, 2) | rv_tb(c, 5, 3))
#define rvc_imm_cl(c)   /* CL imm. for c.lw/c.sw */ (rv_tb(c, 5, 6) | rv_tbf(c, 12, 10, 3) | rv_tb(c, 6, 2))
#define rvc_imm_ci(c)   /* CI imm. for c.addi/c.li/c.lui */ (rv_signext(rv_tb(c, 12, 5), 5) | rv_bf(c, 6, 2))
#define rvc_imm_ci_b(c) /* CI imm. for c.addi16sp */                                                                   \
  (rv_signext(rv_tb(c, 12, 9), 9) | rv_tbf(c, 4, 3, 7) | rv_tb(c, 5, 6) | rv_tb(c, 2, 5) | rv_tb(c, 6, 4))
#define rvc_imm_ci_c(c) /* CI imm. for c.lwsp */ (rv_tbf(c, 3, 2, 6) | rv_tb(c, 12, 5) | rv_tbf(c, 6, 4, 2))
#define rvc_imm_cj(c)   /* CJ imm. for c.jalr/c.j */                                                                   \
  (rv_signext(rv_tb(c, 12, 11), 11) | rv_tb(c, 11, 4) | rv_tbf(c, 10, 9, 8) | rv_tb(c, 8, 10) | rv_tb(c, 7, 6) |       \
   rv_tb(c, 6, 7) | rv_tbf(c, 5, 3, 1) | rv_tb(c, 2, 5))
#define rvc_imm_cb(c) /* CB imm. for c.beqz/c.bnez */                                                                  \
  (rv_signext(rv_tb(c, 12, 8), 8) | rv_tbf(c, 6, 5, 6) | rv_tb(c, 2, 5) | rv_tbf(c, 11, 10, 3) | rv_tbf(c, 4, 3, 1))
#define rvc_imm_css(c) /* CSS imm. for c.swsp */ (rv_tbf(c, 8, 7, 6) | rv_tbf(c, 12, 9, 2))

/* macros to assemble all uncompressed instruction types */
#define rv_i_i(op, f3, rd, rs1, imm)  /* I-type */ ((imm) << 20 | (rs1) << 15 | (f3) << 12 | (rd) << 7 | (op) << 2 | 3)
#define rv_i_s(op, f3, rs1, rs2, imm) /* S-type */                                                                     \
  (rv_bf(imm, 11, 5) << 25 | (rs2) << 20 | (rs1) << 15 | (f3) << 12 | rv_bf(imm, 4, 0) << 7 | (op) << 2 | 3)
#define rv_i_u(op, rd, imm)              /* U-type */ ((imm) << 12 | (rd) << 7 | (op) << 2 | 3)
#define rv_i_r(op, f3, rd, rs1, rs2, f7) /* R-type */                                                                  \
  ((f7) << 25 | (rs2) << 20 | (rs1) << 15 | (f3) << 12 | (rd) << 7 | (op) << 2 | 3)
#define rv_i_j(op, rd, imm) /* J-type */                                                                               \
  (rv_b(imm, 20) << 31 | rv_bf(imm, 10, 1) << 21 | rv_b(imm, 11) << 20 | rv_bf(imm, 19, 12) << 12 | (rd) << 7 |        \
   (op) << 2 | 3)
#define rv_i_b(op, f3, rs1, rs2, imm) /* B-type */                                                                     \
  (rv_b(imm, 12) << 31 | rv_bf(imm, 10, 5) << 25 | (rs2) << 20 | (rs1) << 15 | (f3) << 12 | rv_bf(imm, 4, 1) << 8 |    \
   rv_b(imm, 11) << 7 | (op) << 2 | 3)

/* decompress instruction */
static u32 rvc(u32 c) {
  if (rvc_op(c) == 0) {
    if (rvc_f3(c) == 0 && c != 0) {
      /* c.addi4spn -> addi rd', x2, nzuimm */
      return rv_i_i(4, 0, rvc_irpl(c), 2, rvc_imm_ciw(c));
    } else if (c == 0) {
      /* illegal */
      return 0;
    } else if (rvc_f3(c) == 2) {
      /*I c.lw -> lw rd', offset(rs1') */
      return rv_i_i(0, 2, rvc_irpl(c), rvc_irph(c), rvc_imm_cl(c));
    } else if (rvc_f3(c) == 6) {
      /*I c.sw -> sw rs2', offset(rs1') */
      return rv_i_s(8, 2, rvc_irph(c), rvc_irpl(c), rvc_imm_cl(c));
    } else {
      /* illegal */
      return 0;
    }
  } else if (rvc_op(c) == 1) {
    if (rvc_f3(c) == 0) {
      /*I c.addi -> addi rd, rd, nzimm */
      return rv_i_i(4, 0, rvc_ird(c), rvc_ird(c), rvc_imm_ci(c));
    } else if (rvc_f3(c) == 1) {
      /*I c.jal -> jal x1, offset */
      return rv_i_j(27, 1, rvc_imm_cj(c));
    } else if (rvc_f3(c) == 2) {
      /*I c.li -> addi rd, x0, imm */
      return rv_i_i(4, 0, rvc_ird(c), 0, rvc_imm_ci(c));
    } else if (rvc_f3(c) == 3) {
      /* 01/011: LUI/ADDI16SP */
      if (rvc_ird(c) == 2) {
        /*I c.addi16sp -> addi x2, x2, nzimm */
        return rv_i_i(4, 0, 2, 2, rvc_imm_ci_b(c));
      } else if (rvc_ird(c) != 0) {
        /*I c.lui -> lui rd, nzimm */
        return rv_i_u(13, rvc_ird(c), rvc_imm_ci(c));
      } else {
        /* illegal */
        return 0;
      }
    } else if (rvc_f3(c) == 4) {
      /* 01/100: MISC-ALU */
      if (rv_bf(c, 11, 10) == 0) {
        /*I c.srli -> srli rd', rd', shamt */
        return rv_i_r(4, 5, rvc_irph(c), rvc_irph(c), rvc_imm_ci(c) & 0x1F, 0);
      } else if (rv_bf(c, 11, 10) == 1) {
        /*I c.srai -> srai rd', rd', shamt */
        return rv_i_r(4, 5, rvc_irph(c), rvc_irph(c), rvc_imm_ci(c) & 0x1F, 32);
      } else if (rv_bf(c, 11, 10) == 2) {
        /*I c.andi -> andi rd', rd', imm */
        return rv_i_i(4, 7, rvc_irph(c), rvc_irph(c), rvc_imm_ci(c));
      } else if (rv_bf(c, 11, 10) == 3) {
        if (rv_bf(c, 6, 5) == 0) {
          /*I c.sub -> sub rd', rd', rs2' */
          return rv_i_r(12, 0, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 32);
        } else if (rv_bf(c, 6, 5) == 1) {
          /*I c.xor -> xor rd', rd', rs2' */
          return rv_i_r(12, 4, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 0);
        } else if (rv_bf(c, 6, 5) == 2) {
          /*I c.or -> or rd', rd', rs2' */
          return rv_i_r(12, 6, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 0);
        } else if (rv_bf(c, 6, 5) == 3) {
          /*I c.and -> and rd', rd', rs2' */
          return rv_i_r(12, 7, rvc_irph(c), rvc_irph(c), rvc_irpl(c), 0);
        } else {
          /* illegal */
          return 0;
        }
      } else {
        /* illegal */
        return 0;
      }
    } else if (rvc_f3(c) == 5) {
      /*I c.j -> jal x0, offset */
      return rv_i_j(27, 0, rvc_imm_cj(c));
    } else if (rvc_f3(c) == 6) {
      /*I c.beqz -> beq rs1' x0, offset */
      return rv_i_b(24, 0, rvc_irph(c), 0, rvc_imm_cb(c));
    } else if (rvc_f3(c) == 7) {
      /*I c.bnez -> bne rs1' x0, offset */
      return rv_i_b(24, 1, rvc_irph(c), 0, rvc_imm_cb(c));
    } else {
      /* illegal */
      return 0;
    }
  } else if (rvc_op(c) == 2) {
    if (rvc_f3(c) == 0) {
      /*I c.slli -> slli rd, rd, shamt */
      return rv_i_r(4, 1, rvc_ird(c), rvc_ird(c), rvc_imm_ci(c) & 0x1F, 0);
    } else if (rvc_f3(c) == 2) {
      /*I c.lwsp -> lw rd, offset(x2) */
      return rv_i_i(0, 2, rvc_ird(c), 2, rvc_imm_ci_c(c));
    } else if (rvc_f3(c) == 4 && !rv_b(c, 12) && !rv_bf(c, 6, 2)) {
      /*I c.jr -> jalr x0, 0(rs1) */
      return rv_i_i(25, 0, 0, rvc_ird(c), 0);
    } else if (rvc_f3(c) == 4 && !rv_b(c, 12)) {
      /*I c.mv -> add rd, x0, rs2 */
      return rv_i_r(12, 0, rvc_ird(c), 0, rv_bf(c, 6, 2), 0);
    } else if (rvc_f3(c) == 4 && rv_b(c, 12) && rvc_ird(c) && !rv_bf(c, 6, 2)) {
      /*I c.jalr -> jalr x1, 0(rs1) */
      return rv_i_i(25, 0, 1, rvc_ird(c), 0);
    } else if (rvc_f3(c) == 4 && rv_b(c, 12) && !rvc_ird(c) && !rv_bf(c, 6, 2)) {
      /*I c.ebreak -> ebreak */
      return rv_i_i(28, 0, 0, 0, 1);
    } else if (rvc_f3(c) == 4 && rv_b(c, 12) && rvc_ird(c) && rv_bf(c, 6, 2)) {
      /*I c.add -> add rd, rd, rs2 */
      return rv_i_r(12, 0, rvc_ird(c), rvc_ird(c), rv_bf(c, 6, 2), 0);
    } else if (rvc_f3(c) == 6) {
      /*I c.swsp -> sw rs2, offset(x2) */
      return rv_i_s(8, 2, 2, rv_bf(c, 6, 2), rvc_imm_css(c));
    } else {
      /* illegal */
      return 0;
    }
  } else {
    /* illegal */
    return 0;
  }
}

void rv_endcvt(u8 *in, u8 *out, u32 width, bool is_store) {
  if (!is_store && width == 1)
    *out = in[0];
  else if (!is_store && width == 2)
    *((u16 *)out) = (u16)in[0] | ((u16)in[1] << 8);
  else if (!is_store && width == 4)
    *((u32 *)out) = (u32)in[0] | ((u32)in[1] << 8) | ((u32)in[2] << 16) | (((u32)in[3]) << 24);
  else if (!is_store && width == 8)
    *((u64 *)out) = (u64)in[0] | ((u64)in[1] << 8) | ((u64)in[2] << 16) | ((u64)in[3] << 24) | ((u64)in[4] << 32) |
                    ((u64)in[5] << 40) | ((u64)in[6] << 48) | ((u64)in[7] << 56);
  else if (width == 1)
    out[0] = *in;
  else if (width == 2)
    out[0] = *(u16 *)in >> 0 & 0xFF, out[1] = (*(u16 *)in >> 8);
  else if (width == 4)
    out[0] = *(u32 *)in >> 0 & 0xFF, out[1] = *(u32 *)in >> 8 & 0xFF, out[2] = *(u32 *)in >> 16 & 0xFF,
    out[3] = *(u32 *)in >> 24 & 0xFF;
  else /* width == 8 */
    out[0] = *(u64 *)in >> 0 & 0xFF, out[1] = *(u64 *)in >> 8 & 0xFF, out[2] = *(u64 *)in >> 16 & 0xFF,
    out[3] = *(u64 *)in >> 24 & 0xFF, out[4] = *(u64 *)in >> 32 & 0xFF, out[5] = *(u64 *)in >> 40 & 0xFF,
    out[6] = *(u64 *)in >> 48 & 0xFF, out[7] = *(u64 *)in >> 56 & 0xFF;
}

/* perform a bus access. access == RV_AW stores data. */
static u32 rv_bus(rv *cpu, u32 *va, u8 *data, u32 width, rv_access access) {
  u32 err, pa /* physical address */;
  u8 ledata[4];
  rv_endcvt(data, ledata, width, 1);
  if (*va & (width - 1)) return RV_BAD_ALIGN;
  if ((err = rv_vmm(cpu, *va, &pa, access))) return err; /* page or access fault */
  if (((pa + width - 1) ^ pa) & ~0xFFFU) /* page bound overrun */ {
    u32 w0 /* load this many bytes from 1st page */ = 0x1000 - (*va & 0xFFF);
    if ((err = bus_error_to_rv_res(mach_bus(cpu->mach, pa, ledata, access == RV_AW, w0))) != RV_OK) return err;
    width -= w0, *va += w0, data += w0;
    if ((err = rv_vmm(cpu, *va, &pa, RV_AW))) return err;
  }
  if ((err = bus_error_to_rv_res(mach_bus(cpu->mach, pa, ledata, access == RV_AW, width))) != RV_OK) return err;
  rv_endcvt(ledata, data, width, 0);
  return 0;
}

/* instruction fetch */
static u32 rv_if(rv *cpu, u32 *i, u32 *tval) {
  u32 err, page = (cpu->pc ^ (cpu->pc + 3)) & ~0xFFFU, pc = cpu->pc;
  if (cpu->pc & 2 || page) {
    /* perform fetch in two 2-byte fetches */
    u32 ia /* first half of instruction */ = 0, ib /* second half */ = 0;
    if ((err = rv_bus(cpu, &pc, (u8 *)&ia, 2, RV_AX))) /* fetch 1st half */
      goto error;
    if (rv_isz(ia) == 4 && (pc += 2, 1) &&             /* if instruction is 4 byte wide */
        (err = rv_bus(cpu, &pc, (u8 *)&ib, 2, RV_AX))) /* fetch 2nd half */
      goto error;                                      /* need pc += 2 above for accurate {page}fault traps */
    *i = (u32)ia | (u32)ib << 16U;
  } else if ((err = rv_bus(cpu, &pc, (u8 *)i, 4, RV_AX))) /* 4-byte fetch */
    goto error;
  cpu->next_pc = cpu->pc + rv_isz(*i);
  *tval = *i; /* tval is original inst for illegal instruction traps */
  if (rv_isz(*i) < 4) *i = rvc(*i & 0xFFFF);
  return RV_OK;
error:
  *tval = pc; /* tval is pc for instruction {page}fault traps */
  return err;
}

/* service interrupts */
static u32 rv_service(rv *cpu) {
  static u32 last_mip = 0;
  if (cpu->csr.mip != last_mip) {
    last_mip = cpu->csr.mip;
  }
  u32 iidx /* interrupt number */, d /* delegated privilege */;
  for (iidx = 12; iidx > 0; iidx--) {
    /* highest -> lowest priority */
    if (!(cpu->csr.mip & cpu->csr.mie & (1 << iidx))) continue; /* interrupt not triggered or not enabled */
    d = (cpu->csr.mideleg & (1 << iidx)) ? RV_PSUPER : RV_PMACH;
    if (d == cpu->priv ? rv_b(cpu->csr.mstatus, d) : (d > cpu->priv)) return rv_trap(cpu, 0x80000000U + iidx, cpu->pc);
  }
  return RV_TRAP_NONE;
}

/* single step */
u32 rv_step(rv *cpu) {
  u32 i, tval, err = rv_if(cpu, &i, &tval); /* fetch instruction into i */

  if (!++cpu->csr.cycle) cpu->csr.cycleh++;               /* add to cycle,cycleh with carry */
  if (err) return rv_trap_bus(cpu, err, tval, RV_AX);     /* instruction fetch error */
  if (rv_isz(i) != 4) return rv_trap(cpu, RV_EILL, tval); /* instruction length invalid */
  if (rv_iopl(i) == 0) {
    if (rv_ioph(i) == 0) {
      /*Q 00/000: LOAD */
      u32 va /* virtual address */ = rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i);
      u32 v /* loaded value */ = 0, w /* value width */, sx /* sign ext. */;
      w = 1 << (rv_if3(i) & 3), sx = ~rv_if3(i) & 4; /*I lb, lh, lw, lbu, lhu */
      if ((err = rv_bus(cpu, &va, (u8 *)&v, w, RV_AR))) return rv_trap_bus(cpu, err, va, RV_AR);
      if ((rv_if3(i) & 3) == 3) return rv_trap(cpu, RV_EILL, tval); /* ld instruction not supported */
      if (sx) v = rv_signext(v, (w * 8 - 1));
      rv_sr(cpu, rv_ird(i), v);
    } else if (rv_ioph(i) == 1) {
      /*Q 01/000: STORE */
      u32 va /* virtual address */ = rv_lr(cpu, rv_irs1(i)) + rv_iimm_s(i);
      u32 w /* value width */ = 1 << (rv_if3(i) & 3);
      u32 y /* stored value */ = rv_lr(cpu, rv_irs2(i));
      if (rv_if3(i) > 2)                    /*I sb, sh, sw */
        return rv_trap(cpu, RV_EILL, tval); /* sd instruction not supported */
      if ((err = rv_bus(cpu, &va, (u8 *)&y, w, RV_AW))) return rv_trap_bus(cpu, err, va, RV_AW);
    } else if (rv_ioph(i) == 3) {
      /*Q 11/000: BRANCH */
      u32 a = rv_lr(cpu, rv_irs1(i)), b = rv_lr(cpu, rv_irs2(i));
      u32 y /* comparison value */ = a - b;
      u32 zero = !y, sgn = rv_sgn(y), ovf = rv_ovf(a, b, y), carry = y > a;
      u32 targ = cpu->pc + rv_iimm_b(i);      /* computed branch target */
      if ((rv_if3(i) == 0 && zero) ||         /*I beq */
          (rv_if3(i) == 1 && !zero) ||        /*I bne */
          (rv_if3(i) == 4 && (sgn != ovf)) || /*I blt */
          (rv_if3(i) == 5 && (sgn == ovf)) || /*I bge */
          (rv_if3(i) == 6 && carry) ||        /*I bltu */
          (rv_if3(i) == 7 && !carry)          /*I bgtu */
      ) {
        cpu->next_pc = targ; /* take branch */
      } else if (rv_if3(i) == 2 || rv_if3(i) == 3)
        return rv_trap(cpu, RV_EILL, tval);
      /* default: don't take branch [fall through here] */
    } else
      return rv_trap(cpu, RV_EILL, tval);
  } else if (rv_iopl(i) == 1) {
    if (rv_ioph(i) == 3 && rv_if3(i) == 0) {
      /*Q 11/001: JALR */
      u32 target = (rv_lr(cpu, rv_irs1(i)) + rv_iimm_i(i)); /*I jalr */
      rv_sr(cpu, rv_ird(i), cpu->next_pc);
      cpu->next_pc = target & ~1U; /* target is two-byte aligned */
    } else
      return rv_trap(cpu, RV_EILL, tval);
  } else if (rv_iopl(i) == 3) {
    if (rv_ioph(i) == 0) {
      /*Q 00/011: MISC-MEM */
      if (rv_if3(i) == 0) {
        /*I fence */
        u32 fm = rv_bf(i, 31, 28); /* extract fm field */
        if (fm && fm != 8) return rv_trap(cpu, RV_EILL, tval);
      } else if (rv_if3(i) == 1) {
        /*I fence.i */
      } else
        return rv_trap(cpu, RV_EILL, tval);
    } else if (rv_ioph(i) == 1) {
      /*Q 01/011: AMO */
      u32 va /* address */ = rv_lr(cpu, rv_irs1(i));
      u32 b /* argument */ = rv_lr(cpu, rv_irs2(i));
      u32 x /* loaded value */ = 0, y /* stored value */ = b;
      u32 l /* should load? */ = rv_if5(i) != 3, s /* should store? */ = 1;
      if (rv_bf(i, 14, 12) != 2) {
        /* width must be 2 */
        return rv_trap(cpu, RV_EILL, tval);
      } else {
        if (l && (err = rv_bus(cpu, &va, (u8 *)&x, 4, RV_AR))) return rv_trap_bus(cpu, err, va, RV_AR);
        if (rv_if5(i) == 0) /*I amoadd.w */
          y = x + b;
        else if (rv_if5(i) == 1) /*I amoswap.w */
          y = b;
        else if (rv_if5(i) == 2 && !b) /*I lr.w */
          cpu->res = va, cpu->res_valid = 1, s = 0;
        else if (rv_if5(i) == 3) /*I sc.w */
          x = !(cpu->res_valid && cpu->res_valid-- && cpu->res == va), s = !x;
        else if (rv_if5(i) == 4) /*I amoxor.w */
          y = x ^ b;
        else if (rv_if5(i) == 8) /*I amoor.w */
          y = x | b;
        else if (rv_if5(i) == 12) /*I amoand.w */
          y = x & b;
        else if (rv_if5(i) == 16) /*I amomin.w */
          y = rv_sgn(x - b) != rv_ovf(x, b, x - b) ? x : b;
        else if (rv_if5(i) == 20) /*I amomax.w */
          y = rv_sgn(x - b) == rv_ovf(x, b, x - b) ? x : b;
        else if (rv_if5(i) == 24) /*I amominu.w */
          y = (x - b) > x ? x : b;
        else if (rv_if5(i) == 28) /*I amomaxu.w */
          y = (x - b) <= x ? x : b;
        else
          return rv_trap(cpu, RV_EILL, tval);
        if (s && (err = rv_bus(cpu, &va, (u8 *)&y, 4, RV_AW))) return rv_trap_bus(cpu, err, va, RV_AW);
      }
      rv_sr(cpu, rv_ird(i), x);
    } else if (rv_ioph(i) == 3) {
      /*Q 11/011: JAL */
      rv_sr(cpu, rv_ird(i), cpu->next_pc); /*I jal */
      cpu->next_pc = cpu->pc + rv_iimm_j(i);
    } else
      return rv_trap(cpu, RV_EILL, tval);
  } else if (rv_iopl(i) == 4) {
    /* ALU section */
    if (rv_ioph(i) == 0 || /*Q 00/100: OP-IMM */
        rv_ioph(i) == 1) {
      /*Q 01/100: OP */
      u32 a = rv_lr(cpu, rv_irs1(i)), b = rv_ioph(i) ? rv_lr(cpu, rv_irs2(i)) : rv_iimm_i(i),
          s /* alt. ALU op */ = (rv_ioph(i) || rv_if3(i)) ? rv_b(i, 30) : 0, y /* result */,
          sh /* shift amount */ = b & 0x1F;
      if (!rv_ioph(i) || !rv_b(i, 25)) {
        if (rv_if3(i) == 0)      /*I add, addi, sub */
          y = s ? a - b : a + b; /* subtract if alt. op, otherwise add */
        else if ((rv_if3(i) == 5 || rv_if3(i) == 1) && b >> 5 & 0x5F && !rv_ioph(i))
          return rv_trap(cpu, RV_EILL, tval); /* shift too big! */
        else if (rv_if3(i) == 1)              /*I sll, slli */
          y = a << sh;
        else if (rv_if3(i) == 2) /*I slt, slti */
          y = rv_ovf(a, b, a - b) != rv_sgn(a - b);
        else if (rv_if3(i) == 3) /*I sltu, sltiu */
          y = (a - b) > a;
        else if (rv_if3(i) == 4) /*I xor, xori */
          y = a ^ b;
        else if (rv_if3(i) == 5) /*I srl, srli, sra, srai */
          y = a >> (sh & 31) | (0U - (s && rv_sgn(a))) << (31 - (sh & 31));
        else if (rv_if3(i) == 6) /*I or, ori */
          y = a | b;
        else /*I and, andi */
          y = a & b;
      } else if (rv_ioph(i) == 1 && rv_if7(i) == 1) {
        u32 as /* sgn(a) */ = 0, bs /* sgn(b) */ = 0, ylo, yhi /* result */;
        if (rv_if3(i) < 4) {
          /*I mul, mulh, mulhsu, mulhu */
          if (rv_if3(i) < 3 && rv_sgn(a))      /* a is signed iff f3 in {0, 1, 2} */
            a = ~a + 1, as = 1;                /* two's complement */
          if (rv_if3(i) < 2 && rv_sgn(b))      /* b is signed iff f3 in {0, 1} */
            b = ~b + 1, bs = 1;                /* two's complement */
          ylo = rvm(a, b, &yhi);               /* perform multiply */
          if (as != bs)                        /* invert output quantity if result <0 */
            ylo = ~ylo + 1, yhi = ~yhi + !ylo; /* two's complement */
          y = rv_if3(i) ? yhi : ylo;           /* return hi word if mulh, otherwise lo */
        } else {
          if (rv_if3(i) == 4) /*I div */
            y = b ? (u32)((s32)a / (s32)b) : (u32)(-1);
          else if (rv_if3(i) == 5) /*I divu */
            y = b ? (a / b) : (u32)(-1);
          else if (rv_if3(i) == 6) /*I rem */
            y = b ? (u32)((s32)a % (s32)b) : a;
          else /*I remu */
            y = b ? a % b : a;
        } /* all this because we don't have 64bits. worth it? probably not B) */
      } else {
        return rv_trap(cpu, RV_EILL, tval);
      }
      rv_sr(cpu, rv_ird(i), y); /* set register to ALU output */
    } else if (rv_ioph(i) == 3) {
      /*Q 11/100: SYSTEM */
      u32 csr /* CSR number */ = rv_iimm_iu(i), y /* result */;
      u32 s /* uimm */ = rv_if3(i) & 4 ? rv_irs1(i) : rv_lr(cpu, rv_irs1(i));
      if ((rv_if3(i) & 3) == 1) {
        /*I csrrw, csrrwi */
        if (rv_irs1(i)) {
          /* perform CSR load */
          if (rv_csr_bus(cpu, csr, 0, &y)) /* load CSR into y */
            return rv_trap(cpu, RV_EILL, tval);
          if (rv_ird(i)) rv_sr(cpu, rv_ird(i), y); /* store y into rd */
        }
        if (rv_csr_bus(cpu, csr, 1, &s)) /* set CSR to s */
          return rv_trap(cpu, RV_EILL, tval);
      } else if ((rv_if3(i) & 3) == 2) {
        /*I csrrs, csrrsi */
        if (rv_csr_bus(cpu, csr, 0, &y)) /* load CSR into y */
          return rv_trap(cpu, RV_EILL, tval);
        rv_sr(cpu, rv_ird(i), y), y |= s;              /* store y into rd  */
        if (rv_irs1(i) && rv_csr_bus(cpu, csr, 1, &y)) /*     s|y into CSR */
          return rv_trap(cpu, RV_EILL, tval);
      } else if ((rv_if3(i) & 3) == 3) {
        /*I csrrc, csrrci */
        if (rv_csr_bus(cpu, csr, 0, &y)) /* load CSR into y */
          return rv_trap(cpu, RV_EILL, tval);
        rv_sr(cpu, rv_ird(i), y), y &= ~s;             /* store y into rd  */
        if (rv_irs1(i) && rv_csr_bus(cpu, csr, 1, &y)) /*    ~s&y into CSR */
          return rv_trap(cpu, RV_EILL, tval);
      } else if (!rv_if3(i)) {
        if (!rv_ird(i)) {
          if (!rv_irs1(i) && rv_irs2(i) == 2 && (rv_if7(i) == 8 || rv_if7(i) == 24)) {
            /*I mret, sret */
            u32 xp /* instruction privilege */ = rv_if7(i) >> 3;
            u32 yp /* previous (incoming) privilege [either mpp or spp] */ =
                cpu->csr.mstatus >> (xp == RV_PMACH ? 11 : 8) & xp;
            u32 xpie /* previous ie bit */ = rv_b(cpu->csr.mstatus, 4 + xp);
            u32 mprv /* modify privilege */ = rv_b(cpu->csr.mstatus, 17);
            if (rv_b(cpu->csr.mstatus, 22) && xp == RV_PSUPER)
              return rv_trap(cpu, RV_EILL, tval);            /* exception if tsr=1 */
            mprv *= yp == RV_PMACH;                          /* if y != m, mprv' = 0 */
            cpu->csr.mstatus &= xp == RV_PMACH ? 0xFFFDE777  /* {mpp, mie, mpie, mprv} <- 0 */
                                               : 0xFFFDFEDD; /* {spp, sie, spie, mprv} <- 0 */
            cpu->csr.mstatus |= xpie << xp                   /* xie <- xpie */
                                | 1 << (4 + xp)              /* xpie <- 1 */
                                | mprv << 17;                /* mprv <- mprv' */
            cpu->priv = yp;                                  /* priv <- y */
            cpu->next_pc = xp == RV_PMACH ? cpu->csr.mepc : cpu->csr.sepc;
          } else if (rv_irs2(i) == 5 && rv_if7(i) == 8) {
            /*I wfi */
            cpu->pc = cpu->next_pc;
            return (err = rv_service(cpu)) == RV_TRAP_NONE ? RV_TRAP_WFI : err;
          } else if (rv_if7(i) == 9) {
            /*I sfence.vma */
            if (cpu->priv == RV_PSUPER && (cpu->csr.mstatus & (1 << 20))) return rv_trap(cpu, RV_EILL, tval);
            /* Invalidate all TLB entries */
            for (u32 s = 0; s < RV_ITLB_SETS; s++) {
              for (u32 w = 0; w < RV_ITLB_WAYS; w++) {
                cpu->itlb[s][w].valid = 0;
              }
            }
            for (u32 s = 0; s < RV_DTLB_SETS; s++) {
              for (u32 w = 0; w < RV_DTLB_WAYS; w++) {
                cpu->dtlb[s][w].valid = 0;
              }
            }
            for (u32 i = 0; i < RV_STLB_ENTRIES; i++) {
              cpu->stlb[i].valid = 0;
            }
          } else if (!rv_irs1(i) && !rv_irs2(i) && !rv_if7(i)) {
            /*I ecall */
            return rv_trap(cpu, RV_EUECALL + cpu->priv, cpu->pc);
          } else if (!rv_irs1(i) && rv_irs2(i) == 1 && !rv_if7(i)) {
            return rv_trap(cpu, RV_EBP, cpu->pc); /*I ebreak */
          } else
            return rv_trap(cpu, RV_EILL, tval);
        } else
          return rv_trap(cpu, RV_EILL, tval);
      } else
        return rv_trap(cpu, RV_EILL, tval);
    } else
      return rv_trap(cpu, RV_EILL, tval);
  } else if (rv_iopl(i) == 5) {
    if (rv_ioph(i) == 0) {
      /*Q 00/101: AUIPC */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i) + cpu->pc); /*I auipc */
    } else if (rv_ioph(i) == 1) {
      /*Q 01/101: LUI */
      rv_sr(cpu, rv_ird(i), rv_iimm_u(i)); /*I lui */
    } else
      return rv_trap(cpu, RV_EILL, tval);
  } else
    return rv_trap(cpu, RV_EILL, tval);
  cpu->pc = cpu->next_pc;
  if (cpu->csr.mip && (err = rv_service(cpu)) != RV_TRAP_NONE) return err;
  return RV_TRAP_NONE; /* reserved code -- no exception */
}

void rv_irq(rv *cpu, rv_cause cause) {
  cpu->csr.mip &= ~(u32)(RV_CSI | RV_CTI | RV_CEI);
  cpu->csr.mip |= cause;
}
