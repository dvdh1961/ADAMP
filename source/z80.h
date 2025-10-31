#ifndef Z80_H_
#define Z80_H_

// --- Start: C++ Guard ---
// Dit zorgt ervoor dat C++ code de C-functies correct kan aanroepen.
#ifdef __cplusplus
extern "C" {
#endif

// --- Originele Defines en Types ---

typedef unsigned char       UINT8;
typedef unsigned short      UINT16;
typedef unsigned int        UINT32;
//__extension__ typedef unsigned long long  UINT64;
typedef signed char         INT8;
typedef signed short        INT16;
typedef signed int          INT32;
//__extension__ typedef signed long long  INT64;


/******************************************************************************
 * Union of UINT8, UINT16 and UINT32 in native endianess of the target
 * This is used to access bytes and words in a machine independent manner.
 * The upper bytes h2 and h3 normally contain zero (16 bit CPU cores)
 * thus PAIR.d can be used to pass arguments to the memory system
 * which expects 'int' really.
 ******************************************************************************/
typedef union {
    struct { UINT8 l,h,h2,h3; } b;
    struct { UINT16 l,h; } w;
    UINT32 d;
}  PAIR;


/* Interrupt line constants */
enum
{
    /* line states */
    CLEAR_LINE = 0,    /* clear (a fired, held or pulsed) line */
    ASSERT_LINE,       /* assert an interrupt immediately */
    HOLD_LINE,         /* hold interrupt line until acknowledged */
    PULSE_LINE,        /* pulse interrupt line for one instruction */

    /* internal flags (not for use by drivers!) */
    INTERNAL_CLEAR_LINE = 100 + CLEAR_LINE,
    INTERNAL_ASSERT_LINE = 100 + ASSERT_LINE,

    /* input lines */
    MAX_INPUT_LINES = 32+3,
    INPUT_LINE_IRQ0 = 0,
    INPUT_LINE_IRQ1 = 1,
    INPUT_LINE_IRQ2 = 2,
    INPUT_LINE_IRQ3 = 3,
    INPUT_LINE_IRQ4 = 4,
    INPUT_LINE_IRQ5 = 5,
    INPUT_LINE_IRQ6 = 6,
    INPUT_LINE_IRQ7 = 7,
    INPUT_LINE_IRQ8 = 8,
    INPUT_LINE_IRQ9 = 9,
    INPUT_LINE_NMI = MAX_INPUT_LINES - 3,

    /* special input lines that are implemented in the core */
    INPUT_LINE_RESET = MAX_INPUT_LINES - 2,
    INPUT_LINE_HALT = MAX_INPUT_LINES - 1,

    /* output lines */
    MAX_OUTPUT_LINES = 32
};


/* daisy-chain link */
typedef struct {
    void (*reset)(int);        /* reset callback     */
    int  (*interrupt_entry)(int);  /* entry callback     */
    void (*interrupt_reti)(int);   /* reti callback      */
    int irq_param;              /* callback paramater */
}  Z80_DaisyChain;

#define Z80_MAXDAISY  4   /* maximum of daisy chan device */

#define Z80_INT_REQ     0x01    /* interrupt request mask         */
#define Z80_INT_IEO     0x02    /* interrupt disable mask(IEO)  */

#define Z80_VECTOR(device,state) (((device)<<8)|(state))

enum
{
    Z80_PC, Z80_SP,
    Z80_A, Z80_B, Z80_C, Z80_D, Z80_E, Z80_H, Z80_L,
    Z80_AF, Z80_BC, Z80_DE, Z80_HL,
    Z80_IX, Z80_IY,  Z80_AF2, Z80_BC2, Z80_DE2, Z80_HL2,
    Z80_R, Z80_I, Z80_IM, Z80_IFF1, Z80_IFF2, Z80_HALT,
    Z80_DC0, Z80_DC1, Z80_DC2, Z80_DC3, Z80_WZ
};

enum {
    Z80_TABLE_op,
    Z80_TABLE_cb,
    Z80_TABLE_ed,
    Z80_TABLE_xy,
    Z80_TABLE_xycb,
    Z80_TABLE_ex  /* cycles counts for taken jr/jp/call and interrupt latency (rst opcodes) */
};

/****************************************************************************/
/* The Z80 registers. HALT is set to 1 when the CPU is halted, the refresh  */
/* register is calculated as follows: refresh=(Z80.r&127)|(Z80.r2&128)      */
/****************************************************************************/
typedef struct
{
    PAIR  pc,sp,af,bc,de,hl,ix,iy,wz;
    PAIR  af2,bc2,de2,hl2;
    UINT8  r,r2,iff1,iff2,halt,im,i;
    UINT8  nmi_state;     /* nmi line state */
    UINT8  nmi_pending;   /* nmi pending */
    UINT8  irq_state;     /* irq line state */
    UINT8  after_ei;      /* are we in the EI shadow? */
    const struct z80_irq_daisy_chain *daisy;
    int    (*irq_callback)(int irqline);
}  Z80_Regs;


// --- ALLE Functiedeclaraties ---

extern int z80_cycle_count;
extern Z80_Regs Z80;

extern void take_interrupt(void);
extern int z80_ICount;
extern int z80_exec;
extern int z80_cycle_count;
extern int z80_requested_cycles;
extern int z80_checknmi(void);
extern int z80_do_opcode(void);

extern void z80_init(void);
extern void z80_reset (void);
extern void z80_exit (void);
extern int z80_execute(int cycles);
extern void z80_burn(int cycles);
extern void z80_get_context (void *dst);
extern void z80_set_context (void *src);
extern void z80_set_irq_line(int irqline, int state);
extern void z80_reset_cycle_count(void);
extern int z80_get_elapsed_cycles(void);

/*
 * Deze functies worden elders (in coleco.cpp) gedefinieerd
 * en AANGEROEPEN door z80.c.
 */
extern unsigned int cpu_readmem16(unsigned int address);
extern void cpu_writemem16(unsigned int address, unsigned int value);
extern unsigned char cpu_readport(unsigned int port);
extern void cpu_writeport(unsigned int port, unsigned char value);
extern unsigned int cpu_readport16(unsigned int port);
extern void cpu_writeport16(unsigned int port, unsigned int value);


// --- Einde: C++ Guard ---
#ifdef __cplusplus
}
#endif

#endif // Z80_H_
