#ifndef RISCV_DEFS_H__
#define RISCV_DEFS_H__

#include <fomu/csr.h>

#define CSR_MSTATUS_MIE 0x8

#define CSR_IRQ_MASK 0xBC0
#define CSR_IRQ_PENDING 0xFC0

#define CSR_DCACHE_INFO 0xCC0

#ifdef __cplusplus
extern "C" {
#endif

void flush_cpu_icache(void);
void flush_cpu_dcache(void);
void flush_l2_cache(void);

#define csrr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

#define csrw(reg, val) ({ \
  if (__builtin_constant_p(val) && (unsigned long)(val) < 32) \
	asm volatile ("csrw " #reg ", %0" :: "i"(val)); \
  else \
	asm volatile ("csrw " #reg ", %0" :: "r"(val)); })

#define csrs(reg, bit) ({ \
  if (__builtin_constant_p(bit) && (unsigned long)(bit) < 32) \
	asm volatile ("csrrs x0, " #reg ", %0" :: "i"(bit)); \
  else \
	asm volatile ("csrrs x0, " #reg ", %0" :: "r"(bit)); })

#define csrc(reg, bit) ({ \
  if (__builtin_constant_p(bit) && (unsigned long)(bit) < 32) \
	asm volatile ("csrrc x0, " #reg ", %0" :: "i"(bit)); \
  else \
	asm volatile ("csrrc x0, " #reg ", %0" :: "r"(bit)); })

#ifdef __cplusplus
}
#endif

__attribute__((noreturn)) void reboot(void);

__attribute__((noreturn)) static inline void warmboot_to_image(uint8_t image_index) {
	reboot_ctrl_write(0xac | (image_index & 3) << 0);
	while (1);
}

#endif	/* RISCV_DEFS_H__ */
