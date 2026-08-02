#include "f18a_gpu.h"

#include <string.h>

#define GPU_ST_LGT 0x8000u
#define GPU_ST_AGT 0x4000u
#define GPU_ST_EQ  0x2000u
#define GPU_ST_C   0x1000u
#define GPU_ST_OV  0x0800u
#define GPU_ST_OP  0x0400u

typedef struct GpuOperand
{
    uint16_t address;
    uint8_t reg;
    uint8_t mode;
    uint8_t byte_op;
} GpuOperand;

static size_t gpu_address(const F18aGpu *gpu, uint16_t address)
{
    /* F18A GRAM1 is mirrored twice and palette RAM 32 times. */
    if (address >= 0x4000u && address < 0x5000u)
        address = (uint16_t)(0x4000u | (address & 0x07FFu));
    else if (address >= 0x5000u && address < 0x6000u)
        address = (uint16_t)(0x5000u | (address & 0x007Fu));
    return gpu->memory_size ? (size_t)address % gpu->memory_size : 0u;
}

static uint8_t gpu_read_byte(const F18aGpu *gpu, uint16_t address)
{
    return gpu->memory[gpu_address(gpu, address)];
}

static void gpu_write_byte(F18aGpu *gpu, uint16_t address, uint8_t value)
{
    gpu->memory[gpu_address(gpu, address)] = value;
}

static uint16_t gpu_read_word(const F18aGpu *gpu, uint16_t address)
{
    /* The TMS9900 has a 16-bit bus: every word access is aligned.  Both the
       real F18A GPU and the reference implementations discard A0 here. */
    address &= 0xFFFEu;
    return (uint16_t)(((uint16_t)gpu_read_byte(gpu, address) << 8) |
                      gpu_read_byte(gpu, (uint16_t)(address + 1u)));
}

static void gpu_write_word(F18aGpu *gpu, uint16_t address, uint16_t value)
{
    address &= 0xFFFEu;
    gpu_write_byte(gpu, address, (uint8_t)(value >> 8));
    gpu_write_byte(gpu, (uint16_t)(address + 1u), (uint8_t)value);
}

static uint16_t gpu_reg_read(const F18aGpu *gpu, unsigned int reg)
{
    if (gpu->wp == 0xFFFEu)
        return gpu->workspace[reg & 15u];
    return gpu_read_word(gpu, (uint16_t)(gpu->wp + ((reg & 15u) << 1)));
}

static void gpu_reg_write(F18aGpu *gpu, unsigned int reg, uint16_t value)
{
    if (gpu->wp == 0xFFFEu) {
        gpu->workspace[reg & 15u] = value;
        return;
    }
    gpu_write_word(gpu, (uint16_t)(gpu->wp + ((reg & 15u) << 1)), value);
}

static void gpu_set_lae_word(F18aGpu *gpu, uint16_t value)
{
    gpu->st &= (uint16_t)~(GPU_ST_LGT | GPU_ST_AGT | GPU_ST_EQ);
    if (value == 0u)
        gpu->st |= GPU_ST_EQ;
    else {
        gpu->st |= GPU_ST_LGT;
        if ((value & 0x8000u) == 0u)
            gpu->st |= GPU_ST_AGT;
    }
}

static void gpu_set_lae_byte(F18aGpu *gpu, uint8_t value)
{
    gpu->st &= (uint16_t)~(GPU_ST_LGT | GPU_ST_AGT | GPU_ST_EQ);
    if (value == 0u)
        gpu->st |= GPU_ST_EQ;
    else {
        gpu->st |= GPU_ST_LGT;
        if ((value & 0x80u) == 0u)
            gpu->st |= GPU_ST_AGT;
    }
}

static void gpu_set_parity(F18aGpu *gpu, uint8_t value)
{
    unsigned int bits = 0u;
    unsigned int i;
    for (i = 0u; i < 8u; ++i)
        bits += (value >> i) & 1u;
    gpu->st &= (uint16_t)~GPU_ST_OP;
    if ((bits & 1u) != 0u)
        gpu->st |= GPU_ST_OP;
}

static GpuOperand gpu_resolve(F18aGpu *gpu, unsigned int spec, int byte_op)
{
    GpuOperand op;
    uint16_t base;
    op.reg = (uint8_t)(spec & 15u);
    op.mode = (uint8_t)((spec >> 4) & 3u);
    op.byte_op = (uint8_t)(byte_op != 0);
    base = (uint16_t)(gpu->wp + ((uint16_t)op.reg << 1));

    switch (op.mode) {
    case 0u:
        op.address = base;
        break;
    case 1u:
    case 3u:
        op.address = gpu_reg_read(gpu, op.reg);
        break;
    default: {
        const uint16_t displacement = gpu_read_word(gpu, gpu->pc);
        gpu->pc = (uint16_t)(gpu->pc + 2u);
        op.address = displacement;
        if (op.reg != 0u)
            op.address = (uint16_t)(op.address + gpu_reg_read(gpu, op.reg));
        break;
    }
    }
    return op;
}

static uint16_t gpu_operand_read(const F18aGpu *gpu, const GpuOperand *op)
{
    if (op->mode == 0u) {
        const uint16_t value = gpu_reg_read(gpu, op->reg);
        return op->byte_op ? (uint16_t)(value >> 8) : value;
    }
    return op->byte_op ? gpu_read_byte(gpu, op->address)
                       : gpu_read_word(gpu, op->address);
}

static void gpu_operand_write(F18aGpu *gpu, const GpuOperand *op, uint16_t value)
{
    if (op->mode == 0u) {
        if (op->byte_op)
            gpu_reg_write(gpu, op->reg,
                          (uint16_t)((value << 8) | (gpu_reg_read(gpu, op->reg) & 0x00FFu)));
        else
            gpu_reg_write(gpu, op->reg, value);
    } else if (op->byte_op)
        gpu_write_byte(gpu, op->address, (uint8_t)value);
    else
        gpu_write_word(gpu, op->address, value);
}

static void gpu_operand_finish(F18aGpu *gpu, const GpuOperand *op)
{
    if (op->mode == 3u)
        gpu_reg_write(gpu, op->reg,
                      (uint16_t)(gpu_reg_read(gpu, op->reg) +
                                 (op->byte_op ? 1u : 2u)));
}

static void gpu_set_result(F18aGpu *gpu, uint16_t value, int byte_op)
{
    if (byte_op) {
        gpu_set_lae_byte(gpu, (uint8_t)value);
        gpu_set_parity(gpu, (uint8_t)value);
    } else
        gpu_set_lae_word(gpu, value);
}

static uint16_t gpu_add(F18aGpu *gpu, uint16_t left, uint16_t right, int byte_op)
{
    const uint32_t mask = byte_op ? 0xFFu : 0xFFFFu;
    const uint32_t sign = byte_op ? 0x80u : 0x8000u;
    const uint32_t sum = (left & mask) + (right & mask);
    const uint16_t result = (uint16_t)(sum & mask);
    gpu->st &= (uint16_t)~(GPU_ST_C | GPU_ST_OV);
    if (sum > mask)
        gpu->st |= GPU_ST_C;
    if (((~(left ^ right) & (left ^ result)) & sign) != 0u)
        gpu->st |= GPU_ST_OV;
    gpu_set_result(gpu, result, byte_op);
    return result;
}

static uint16_t gpu_sub(F18aGpu *gpu, uint16_t left, uint16_t right, int byte_op)
{
    const uint32_t mask = byte_op ? 0xFFu : 0xFFFFu;
    const uint32_t sign = byte_op ? 0x80u : 0x8000u;
    const uint16_t result = (uint16_t)((left - right) & mask);
    gpu->st &= (uint16_t)~(GPU_ST_C | GPU_ST_OV);
    if ((left & mask) >= (right & mask))
        gpu->st |= GPU_ST_C;
    if ((((left ^ right) & (left ^ result)) & sign) != 0u)
        gpu->st |= GPU_ST_OV;
    gpu_set_result(gpu, result, byte_op);
    return result;
}

static void gpu_compare(F18aGpu *gpu, uint16_t left, uint16_t right, int byte_op)
{
    const uint16_t mask = byte_op ? 0x00FFu : 0xFFFFu;
    const uint16_t sign = byte_op ? 0x0080u : 0x8000u;
    const uint16_t lhs = left & mask;
    const uint16_t rhs = right & mask;
    gpu->st &= (uint16_t)~(GPU_ST_LGT | GPU_ST_AGT | GPU_ST_EQ);
    if (lhs == rhs)
        gpu->st |= GPU_ST_EQ;
    if (lhs > rhs)
        gpu->st |= GPU_ST_LGT;
    if ((int16_t)((lhs ^ sign) - sign) > (int16_t)((rhs ^ sign) - sign))
        gpu->st |= GPU_ST_AGT;
}

static int gpu_execute_two_operand(F18aGpu *gpu, uint16_t opcode)
{
    const unsigned int operation = opcode >> 12;
    const int byte_op = (operation & 1u) != 0u;
    GpuOperand source;
    GpuOperand destination;
    uint16_t src;
    uint16_t dst;
    uint16_t result;

    if (operation < 4u)
        return 0;
    source = gpu_resolve(gpu, opcode & 0x003Fu, byte_op);
    src = gpu_operand_read(gpu, &source);
    gpu_operand_finish(gpu, &source);
    destination = gpu_resolve(gpu, (opcode >> 6) & 0x003Fu, byte_op);
    dst = gpu_operand_read(gpu, &destination);

    switch (operation) {
    case 4u: case 5u: result = (uint16_t)(dst & ~src); break; /* SZC(B) */
    case 6u: case 7u: result = gpu_sub(gpu, dst, src, byte_op); break;
    case 8u: case 9u:
        /* TMS9900 C/CB set LGT/AGT from source compared with destination.
           Reversing these operands makes every ordered conditional branch
           following a compare take the opposite path. */
        gpu_compare(gpu, src, dst, byte_op);
        if (byte_op)
            gpu_set_parity(gpu, (uint8_t)src);
        gpu_operand_finish(gpu, &destination);
        return 1;
    case 10u: case 11u: result = gpu_add(gpu, dst, src, byte_op); break;
    case 12u: case 13u: result = src; break;
    default: result = (uint16_t)(dst | src); break; /* SOC(B) */
    }
    if (operation != 6u && operation != 7u && operation != 10u && operation != 11u)
        gpu_set_result(gpu, result, byte_op);
    gpu_operand_write(gpu, &destination, result);
    gpu_operand_finish(gpu, &destination);
    return 1;
}

static int gpu_execute_jump(F18aGpu *gpu, uint16_t opcode)
{
    const unsigned int condition = (opcode >> 8) & 15u;
    const int displacement = (int)(int8_t)(opcode & 0x00FFu) * 2;
    const int lgt = (gpu->st & GPU_ST_LGT) != 0u;
    const int agt = (gpu->st & GPU_ST_AGT) != 0u;
    const int eq = (gpu->st & GPU_ST_EQ) != 0u;
    const int c = (gpu->st & GPU_ST_C) != 0u;
    const int ov = (gpu->st & GPU_ST_OV) != 0u;
    const int op = (gpu->st & GPU_ST_OP) != 0u;
    int take;
    if ((opcode & 0xF000u) != 0x1000u || condition > 12u)
        return 0;
    switch (condition) {
    case 0u: take = 1; break;              /* JMP */
    case 1u: take = !agt && !eq; break;    /* JLT */
    case 2u: take = !agt || eq; break;     /* JLE */
    case 3u: take = eq; break;             /* JEQ */
    case 4u: take = lgt || eq; break;      /* JHE */
    case 5u: take = agt; break;            /* JGT */
    case 6u: take = !eq; break;            /* JNE */
    case 7u: take = !c; break;             /* JNC */
    case 8u: take = c; break;              /* JOC */
    case 9u: take = !ov; break;            /* JNO */
    case 10u: take = !lgt && !eq; break;   /* JL */
    case 11u: take = lgt && !eq; break;    /* JH */
    default: take = op; break;             /* JOP */
    }
    if (take)
        gpu->pc = (uint16_t)(gpu->pc + displacement);
    return 1;
}

static int gpu_execute_shift(F18aGpu *gpu, uint16_t opcode)
{
    const unsigned int operation = opcode & 0xFF00u;
    const unsigned int reg = opcode & 15u;
    unsigned int count = (opcode >> 4) & 15u;
    uint16_t value;
    unsigned int i;
    int original_sign;
    int overflow = 0;
    if (operation < 0x0800u || operation > 0x0B00u)
        return 0;
    if (count == 0u) {
        count = gpu_reg_read(gpu, 0u) & 15u;
        if (count == 0u) count = 16u;
    }
    value = gpu_reg_read(gpu, reg);
    original_sign = (value & 0x8000u) != 0u;
    gpu->st &= (uint16_t)~(GPU_ST_C | GPU_ST_OV);
    for (i = 0u; i < count; ++i) {
        uint16_t carry;
        if (operation == 0x0A00u) {
            carry = value >> 15;
            value = (uint16_t)(value << 1);
            if (((value & 0x8000u) != 0u) != original_sign) overflow = 1;
        } else {
            carry = value & 1u;
            if (operation == 0x0800u)
                value = (uint16_t)((value >> 1) | (value & 0x8000u));
            else if (operation == 0x0900u)
                value >>= 1;
            else
                value = (uint16_t)((value >> 1) | (carry << 15));
        }
        if (carry) gpu->st |= GPU_ST_C; else gpu->st &= (uint16_t)~GPU_ST_C;
    }
    if (overflow) gpu->st |= GPU_ST_OV;
    gpu_reg_write(gpu, reg, value);
    gpu_set_lae_word(gpu, value);
    return 1;
}

static int gpu_execute_format3(F18aGpu *gpu, uint16_t opcode)
{
    const unsigned int operation = opcode & 0xFC00u;
    const unsigned int reg = (opcode >> 6) & 15u;
    GpuOperand source;
    uint16_t src;
    uint16_t dst;
    if (operation < 0x2000u || operation > 0x3C00u ||
        operation == 0x2C00u || operation == 0x3000u || operation == 0x3400u)
        return 0;
    source = gpu_resolve(gpu, opcode & 0x003Fu, 0);
    src = gpu_operand_read(gpu, &source);
    dst = gpu_reg_read(gpu, reg);
    switch (operation) {
    case 0x2000u: /* COC */
        gpu->st &= (uint16_t)~GPU_ST_EQ;
        if ((src & dst) == src) gpu->st |= GPU_ST_EQ;
        break;
    case 0x2400u: /* CZC */
        gpu->st &= (uint16_t)~GPU_ST_EQ;
        if ((src & dst) == 0u) gpu->st |= GPU_ST_EQ;
        break;
    case 0x2800u: /* XOR */
        dst ^= src; gpu_reg_write(gpu, reg, dst); gpu_set_lae_word(gpu, dst);
        break;
    case 0x3800u: { /* MPY: unsigned 16 x 16 -> Rn:Rn+1 */
        const uint32_t product = (uint32_t)dst * src;
        gpu_reg_write(gpu, reg, (uint16_t)(product >> 16));
        gpu_reg_write(gpu, reg + 1u, (uint16_t)product);
        break;
    }
    default: { /* DIV: Rn:Rn+1 / source */
        const uint32_t dividend = ((uint32_t)dst << 16) | gpu_reg_read(gpu, reg + 1u);
        gpu->st &= (uint16_t)~GPU_ST_OV;
        if (src == 0u || src <= dst)
            gpu->st |= GPU_ST_OV;
        else {
            gpu_reg_write(gpu, reg, (uint16_t)(dividend / src));
            gpu_reg_write(gpu, reg + 1u, (uint16_t)(dividend % src));
        }
        break;
    }
    }
    gpu_operand_finish(gpu, &source);
    return 1;
}

static int gpu_execute_immediate(F18aGpu *gpu, uint16_t opcode)
{
    const unsigned int group = opcode & 0xFFE0u;
    const unsigned int reg = opcode & 15u;
    uint16_t value;
    uint16_t current;
    switch (group) {
    case 0x0200u: /* LI */
        value = gpu_read_word(gpu, gpu->pc); gpu->pc += 2u;
        gpu_reg_write(gpu, reg, value); gpu_set_lae_word(gpu, value); return 1;
    case 0x0220u: /* AI */
        value = gpu_read_word(gpu, gpu->pc); gpu->pc += 2u;
        current = gpu_add(gpu, gpu_reg_read(gpu, reg), value, 0);
        gpu_reg_write(gpu, reg, current); return 1;
    case 0x0240u: /* ANDI */
        value = gpu_read_word(gpu, gpu->pc); gpu->pc += 2u;
        current = gpu_reg_read(gpu, reg) & value;
        gpu_reg_write(gpu, reg, current); gpu_set_lae_word(gpu, current); return 1;
    case 0x0260u: /* ORI */
        value = gpu_read_word(gpu, gpu->pc); gpu->pc += 2u;
        current = gpu_reg_read(gpu, reg) | value;
        gpu_reg_write(gpu, reg, current); gpu_set_lae_word(gpu, current); return 1;
    case 0x0280u: /* CI */
        value = gpu_read_word(gpu, gpu->pc); gpu->pc += 2u;
        gpu_compare(gpu, gpu_reg_read(gpu, reg), value, 0); return 1;
    case 0x02A0u: gpu_reg_write(gpu, reg, gpu->wp); return 1; /* STWP */
    case 0x02C0u: gpu_reg_write(gpu, reg, gpu->st); return 1; /* STST */
    case 0x02E0u: gpu->wp = gpu_read_word(gpu, gpu->pc); gpu->pc += 2u; return 1; /* LWPI */
    default: return 0;
    }
}

static int gpu_execute_single(F18aGpu *gpu, uint16_t opcode)
{
    const unsigned int group = opcode & 0xFFC0u;
    GpuOperand op;
    uint16_t value;
    if (group < 0x0400u || group > 0x0740u ||
        group == 0x0400u || group == 0x0480u)
        return 0;
    op = gpu_resolve(gpu, opcode & 0x003Fu, 0);
    value = gpu_operand_read(gpu, &op);
    switch (group) {
    case 0x0440u: gpu->pc = op.address; break; /* B */
    case 0x04C0u: value = 0u; gpu_operand_write(gpu, &op, value); break;
    case 0x0500u: value = gpu_sub(gpu, 0u, value, 0); gpu_operand_write(gpu, &op, value); break;
    case 0x0540u: value = (uint16_t)~value; gpu_operand_write(gpu, &op, value); gpu_set_lae_word(gpu, value); break;
    case 0x0580u: value = gpu_add(gpu, value, 1u, 0); gpu_operand_write(gpu, &op, value); break;
    case 0x05C0u: value = gpu_add(gpu, value, 2u, 0); gpu_operand_write(gpu, &op, value); break;
    case 0x0600u: value = gpu_sub(gpu, value, 1u, 0); gpu_operand_write(gpu, &op, value); break;
    case 0x0640u: value = gpu_sub(gpu, value, 2u, 0); gpu_operand_write(gpu, &op, value); break;
    case 0x0680u: gpu_reg_write(gpu, 11u, gpu->pc); gpu->pc = op.address; break; /* BL */
    case 0x06C0u: value = (uint16_t)((value << 8) | (value >> 8)); gpu_operand_write(gpu, &op, value); break;
    case 0x0700u: value = 0xFFFFu; gpu_operand_write(gpu, &op, value); break;
    case 0x0740u: if (value & 0x8000u) { value = gpu_sub(gpu, 0u, value, 0); gpu_operand_write(gpu, &op, value); } else gpu_set_lae_word(gpu, value); break;
    default: return 0; /* BLWP and unsupported X execution */
    }
    gpu_operand_finish(gpu, &op);
    return 1;
}

void f18a_gpu_init(F18aGpu *gpu, uint8_t *memory, size_t memory_size)
{
    if (!gpu) return;
    memset(gpu, 0, sizeof(*gpu));
    gpu->memory = memory;
    gpu->memory_size = memory_size;
    gpu->wp = 0xFFFEu;
}

void f18a_gpu_reset(F18aGpu *gpu)
{
    uint8_t *memory;
    size_t memory_size;
    if (!gpu) return;
    memory = gpu->memory; memory_size = gpu->memory_size;
    memset(gpu, 0, sizeof(*gpu));
    gpu->memory = memory; gpu->memory_size = memory_size; gpu->wp = 0xFFFEu;
}

void f18a_gpu_start(F18aGpu *gpu, uint16_t address)
{
    if (!gpu || !gpu->memory || gpu->memory_size == 0u) return;
    memset(gpu->workspace, 0, sizeof(gpu->workspace));
    gpu->pc = address; gpu->st = 0u; gpu->last_opcode = 0u;
    gpu->running = 1u; gpu->idle = 0u; gpu->stop_reason = F18A_GPU_STOP_NONE;
}

unsigned int f18a_gpu_execute(F18aGpu *gpu, unsigned int instruction_budget)
{
    unsigned int executed = 0u;
    if (!gpu || !gpu->running) return 0u;
    while (gpu->running && executed < instruction_budget) {
        const uint16_t instruction_pc = gpu->pc;
        const uint16_t opcode = gpu_read_word(gpu, instruction_pc);
        gpu->last_opcode = opcode; gpu->pc = (uint16_t)(gpu->pc + 2u); ++executed;
        if (gpu_execute_immediate(gpu, opcode) || gpu_execute_jump(gpu, opcode) ||
            gpu_execute_shift(gpu, opcode) || gpu_execute_single(gpu, opcode) ||
            gpu_execute_format3(gpu, opcode) || gpu_execute_two_operand(gpu, opcode))
            continue;
        if (opcode == 0x0340u) {
            gpu->running = 0u; gpu->idle = 1u; gpu->stop_reason = F18A_GPU_STOP_IDLE; break;
        }
        gpu->running = 0u; gpu->stop_reason = F18A_GPU_STOP_UNSUPPORTED;
    }
    if (gpu->running && executed == instruction_budget)
        gpu->stop_reason = F18A_GPU_STOP_BUDGET;
    return executed;
}
