#ifndef F18A_GPU_H
#define F18A_GPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum F18aGpuStopReason
{
    F18A_GPU_STOP_NONE = 0,
    F18A_GPU_STOP_IDLE,
    F18A_GPU_STOP_UNSUPPORTED,
    F18A_GPU_STOP_BUDGET
} F18aGpuStopReason;

typedef struct F18aGpu
{
    uint8_t *memory;
    size_t memory_size;
    uint16_t pc;
    uint16_t wp;
    uint16_t st;
    uint16_t workspace[16];
    uint16_t last_opcode;
    uint8_t running;
    uint8_t idle;
    F18aGpuStopReason stop_reason;
} F18aGpu;

void f18a_gpu_init(F18aGpu *gpu, uint8_t *memory, size_t memory_size);
void f18a_gpu_reset(F18aGpu *gpu);
void f18a_gpu_start(F18aGpu *gpu, uint16_t address);
unsigned int f18a_gpu_execute(F18aGpu *gpu, unsigned int instruction_budget);

#ifdef __cplusplus
}
#endif

#endif
