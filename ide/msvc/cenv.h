// C environment definition for Microsoft Visual C++
#ifdef _M_IX86
# define LH_ABI_x86
# define ASM_JMPBUF_SIZE 28
# define ASM_JMPBUF_ALIGN 4
#elif _M_X64
# define LH_ABI_x86_64
# define ASM_JMPBUF_SIZE 256
# define ASM_JMPBUF_ALIGN 16
#elif _M_ARM64
# define LH_ABI_arm64
# define ASM_JMPBUF_SIZE 256
# define ASM_JMPBUF_ALIGN 16
#elif _M_ARM
# define LH_ABI_arm
# define ASM_JMPBUF_SIZE 112
# define ASM_JMPBUF_ALIGN  8
#endif
#define HAS_ASMSETJMP
#define HAS_STRNCAT_S
#define HAS_MEMCPY_S
#define HAS_MEMMOVE_S
#define HAS_STDBOOL_H
#define HAS__ALLOCA