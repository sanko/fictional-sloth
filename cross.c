// ffi_multiplatform_assembly.c

#include <stdbool.h> // For bool type
#include <stddef.h>  // For offsetof, size_t
#include <string.h>  // For memcpy
#include <stdlib.h>  // For exit(), malloc, free
#include <stdint.h>  // For int32_t, uintptr_t
#include <stdio.h>   // For printf, fprintf, perror, and stderr
#include <stdarg.h>  // For va_list, va_start, va_end (for my_printf)
#include <float.h>   // For FLT_MAX, DBL_MAX, etc.
#include <limits.h>  // For INT_MIN, INT_MAX, CHAR_MIN, CHAR_MAX, etc.
#include <wchar.h>   // For wchar_t, WCHAR_MIN, WCHAR_MAX

// --- Platform Detection ---
#if defined(_WIN64)
    #define FFI_OS_WIN64
    #define FFI_ARCH_X64
    #include <windows.h> // For VirtualAlloc, FlushInstructionCache
#elif defined(__linux__) || defined(__unix__)
    #define FFI_OS_LINUX
    #if defined(__x86_64__)
        #define FFI_ARCH_X64
    #elif defined(__aarch64__)
        #define FFI_ARCH_ARM64
    #endif
    // For mmap (Linux specific for executable memory)
    #include <sys/mman.h> // For mmap, munmap
    #include <unistd.h>  // For sysconf(_SC_PAGESIZE)
#elif defined(__APPLE__)
    #define FFI_OS_MACOS
    #if defined(__x86_64__)
        #define FFI_ARCH_X64
    #elif defined(__aarch64__)
        #define FFI_ARCH_ARM64
    #endif
    // For mmap (macOS specific for executable memory)
    #include <sys/mman.h> // For mmap, munmap
    #include <unistd.h>  // For sysconf(_SC_PAGESIZE)
#else
    #error "Unsupported platform for FFI."
#endif

// Enable FFI_TESTING to use double_tap.h macros
#define FFI_TESTING 1
#include "double_tap.h" // Include the Double TAP testing framework

// --- Type Definitions ---
typedef enum {
    FFI_TYPE_UNKNOWN = 0,
    FFI_TYPE_VOID,      // For functions returning void
    FFI_TYPE_BOOL,
    FFI_TYPE_CHAR,
    FFI_TYPE_UCHAR,     // Unsigned Char
    FFI_TYPE_SHORT,
    FFI_TYPE_USHORT,    // Unsigned Short
    FFI_TYPE_INT,
    FFI_TYPE_UINT,      // Unsigned Int
    FFI_TYPE_LONG,      // long is 64-bit on x86-64 Linux/macOS, 32-bit on Win64
    FFI_TYPE_ULONG,     // unsigned long
    FFI_TYPE_LLONG,     // long long (always 64-bit)
    FFI_TYPE_ULLONG,    // unsigned long long
    FFI_TYPE_FLOAT,
    FFI_TYPE_DOUBLE,
    FFI_TYPE_POINTER,
    FFI_TYPE_WCHAR,     // Wide character type
    FFI_TYPE_SIZE_T,    // size_t type
    FFI_TYPE_SCHAR,     // Explicit signed char
    FFI_TYPE_SSHORT,    // Explicit signed short
    FFI_TYPE_SINT,      // Explicit signed int
    FFI_TYPE_SLONG,     // Explicit signed long
    FFI_TYPE_SLLONG,    // Explicit signed long long
    FFI_TYPE_INT128,    // New: 128-bit signed integer (GCC/Clang extension, or struct on MSVC)
    FFI_TYPE_UINT128,   // New: 128-bit unsigned integer (GCC/Clang extension, or struct on MSVC)
} FFI_Type;

// FFI_Argument structure defines how arguments are represented generically.
typedef struct {
    void* value_ptr; // Pointer to the actual value (e.g., &my_int_var)
} FFI_Argument;

// Generic function pointer type for trampoline and target functions
typedef void (*GenericFuncPtr)(void);
typedef void (*GenericTrampolinePtr)(FFI_Argument* args, int num_args, void* return_buffer_ptr);

// Structure to hold a function's signature metadata AND its trampoline code
typedef struct FFI_FunctionSignature {
    const char* debug_name; // For easier identification in debug prints
    FFI_Type return_type;
    int num_params;
    FFI_Type* param_types;  // Array of expected argument types (can be NULL for no args)
    GenericFuncPtr func_ptr;         // Pointer to the actual C function implementation
    size_t trampoline_size; // Size of the generated trampoline code
    GenericTrampolinePtr trampoline_code;  // Pointer to the dynamically generated executable code
} FFI_FunctionSignature;

// Define parameter types for the functions (static const to avoid multiple definitions if in header)
static FFI_Type identity_int_params[] = { FFI_TYPE_INT };
static FFI_Type add_two_ints_params[] = { FFI_TYPE_INT, FFI_TYPE_INT };
static FFI_Type print_float_double_params[] = { FFI_TYPE_FLOAT, FFI_TYPE_DOUBLE };
static FFI_Type print_two_ints_params[] = { FFI_TYPE_INT, FFI_TYPE_INT };

// New parameter type arrays for extended tests
static FFI_Type identity_bool_params[] = { FFI_TYPE_BOOL };
static FFI_Type identity_char_params[] = { FFI_TYPE_CHAR };
static FFI_Type identity_uchar_params[] = { FFI_TYPE_UCHAR };
static FFI_Type identity_short_params[] = { FFI_TYPE_SHORT };
static FFI_Type identity_ushort_params[] = { FFI_TYPE_USHORT };
static FFI_Type identity_long_params[] = { FFI_TYPE_LONG };
static FFI_Type identity_llong_params[] = { FFI_TYPE_LLONG };
static FFI_Type identity_ullong_params[] = { FFI_TYPE_ULLONG };
static FFI_Type identity_float_params[] = { FFI_TYPE_FLOAT };
static FFI_Type identity_double_params[] = { FFI_TYPE_DOUBLE };
static FFI_Type identity_pointer_params[] = { FFI_TYPE_POINTER };
static FFI_Type sum_seven_ints_params[] = { FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT };

// New parameter type arrays for mixed argument tests
static FFI_Type mixed_int_float_ptr_params[] = { FFI_TYPE_INT, FFI_TYPE_FLOAT, FFI_TYPE_POINTER };
static FFI_Type mixed_double_char_int_params[] = { FFI_TYPE_DOUBLE, FFI_TYPE_CHAR, FFI_TYPE_INT };

// NEW: Parameter type arrays for stack spilling tests
static FFI_Type sum_eight_ints_params[] = { FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT };
static FFI_Type sum_nine_doubles_params[] = { FFI_TYPE_DOUBLE, FFI_TYPE_DOUBLE, FFI_TYPE_DOUBLE, FFI_TYPE_DOUBLE, FFI_TYPE_DOUBLE, FFI_TYPE_DOUBLE, FFI_TYPE_DOUBLE, FFI_TYPE_DOUBLE, FFI_TYPE_DOUBLE };
static FFI_Type mixed_gpr_xmm_stack_spill_params[] = {
    FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, FFI_TYPE_INT, // GPRs
    FFI_TYPE_FLOAT, FFI_TYPE_FLOAT, FFI_TYPE_FLOAT, FFI_TYPE_FLOAT, FFI_TYPE_FLOAT, FFI_TYPE_FLOAT, FFI_TYPE_FLOAT, FFI_TYPE_FLOAT, // XMMs
    FFI_TYPE_INT, FFI_TYPE_DOUBLE // Spilled to stack
};

// NEW: Parameter type arrays for wchar_t and size_t
static FFI_Type identity_wchar_params[] = { FFI_TYPE_WCHAR };
static FFI_Type identity_size_t_params[] = { FFI_TYPE_SIZE_T };

// NEW: Parameter type arrays for explicit signed types
static FFI_Type identity_schar_params[] = { FFI_TYPE_SCHAR };
static FFI_Type identity_sshort_params[] = { FFI_TYPE_SSHORT };
static FFI_Type identity_sint_params[] = { FFI_TYPE_SINT };
static FFI_Type identity_slong_params[] = { FFI_TYPE_SLONG };
static FFI_Type identity_sllong_params[] = { FFI_TYPE_SLLONG };

// NEW: Parameter type arrays for 128-bit integers
static FFI_Type identity_int128_params[] = { FFI_TYPE_INT128 };
static FFI_Type identity_uint128_params[] = { FFI_TYPE_UINT128 };


// --- Assembly Instruction Component Defines (x86-64 System V & Win64) ---
#ifdef FFI_ARCH_X64
// Opcodes
#define OPCODE_END_BRANCH_64 0xFA // Last byte of endbr64 (0xF3 0x0F 0x1E 0xFA)
#define OPCODE_PUSH_RBP     0x55
#define OPCODE_MOV_RM64_R64 0x89 // REX.W + 89 /r -> MOV r/m64, r64 (store register to memory or another register)
#define OPCODE_MOV_R64_RM64 0x8B // REX.W + 8B /r -> MOV r64, r/m64 (load from memory or another register to register)
#define OPCODE_SUB_IMM8_RSP 0x83 // SUB r/m64, imm8 (ModR/M 0xE4 for RSP, R/M group 5 for SUB)
#define OPCODE_ADD_IMM8_RSP 0x83 // ADD r/m64, imm8 (ModR/M 0xC4 for RSP, R/M group 0 for ADD)
#define OPCODE_MOV_IMM64_RAX 0xB8 // MOV RAX, imm64
#define OPCODE_CALL_RM64    0xFF // CALL r/m64 (ModR/M 0xD0 for RAX, R/M group 2 for CALL)
#define OPCODE_RET          0xC3
#define OPCODE_PUSH_R12_BYTE 0x54 // Actual byte for PUSH R12 (used with REX.B)
#define OPCODE_POP_R12_BYTE  0x5C // Actual byte for POP R12 (used with REX.B)
#define OPCODE_PUSH_R14_BYTE 0x56 // Actual byte for PUSH R14 (used with REX.B)
#define OPCODE_POP_R14_BYTE  0x5E // Actual byte for POP R14 (used with REX.B)
#define OPCODE_PUSH_RBP      0x55 // Direct opcode for PUSH RBP
#define OPCODE_POP_RBP      0x5D // Direct opcode for POP RBP
#define OPCODE_PUSH_R8_BYTE  0x50 // Actual byte for PUSH R8 (used with REX.B)
#define OPCODE_POP_R8_BYTE   0x58 // Actual byte for POP R8 (used with REX.B)
#define OPCODE_PUSH_R13_BYTE 0x55 // PUSH RBP (0x55), PUSH R13 (0x41 0x55)
#define OPCODE_POP_R13_BYTE  0x5D // POP RBP (0x5D), POP R13 (0x41 0x5D)

// SSE/AVX Prefixes for Floating Point Instructions
#define PREFIX_MOVSS        0xF3 // For MOVSS (float)
#define PREFIX_MOVSD        0xF2 // For MOVSD (double)
#define OPCODE_XMM_MOV_XMM_RM 0x10 // MOVSS/D Reg, r/m
#define OPCODE_XMM_MOV_RM_XMM 0x11 // MOVSS/D r/m, Reg
#define OPCODE_XORPS        0x57 // XORPS XMM, XMM
#define OPCODE_MOVD_XMM_GPR 0x7E // MOVD r/m32, XMM (0x0F 0x7E /r)
#define OPCODE_MOVQ_XMM_GPR 0x7E // MOVQ r/m64, XMM (REX.W + 0x0F 0x7E /r)

// REX Prefixes
#define REX_W_PREFIX        0x48 // REX.W: 64-bit operand size
#define REX_R_BIT           0x04 // REX.R: Extends ModR/M.Reg field
#define REX_X_BIT           0x02 // REX.X: Extends SIB.Index field
#define REX_B_BIT           0x01 // REX.B: Extends ModR/M.R/M, SIB.Base, or opcode Reg field

#define REX_WR_PREFIX       (REX_W_PREFIX | REX_R_BIT) // W=1, R=1, X=0, B=0
#define REX_WB_PREFIX       (REX_W_PREFIX | REX_B_BIT) // W=1, R=0, X=0, B=1

#define REX_BASE_0x40_BIT   0x40 // Base REX bit that is always set for REX prefix to be valid
#define REX_B_PREFIX_32BIT_OP (REX_BASE_0x40_BIT | REX_B_BIT) // 0x41

// Specific REX prefix for PUSH/POP of R12, R14, R8, R13
#define REX_PUSH_POP_R12_PREFIX (REX_BASE_0x40_BIT | REX_B_BIT) // 0x41
#define REX_PUSH_POP_R14_PREFIX (REX_BASE_0x40_BIT | REX_B_BIT) // 0x41
#define REX_PUSH_POP_R13_PREFIX (REX_BASE_0x40_BIT | REX_B_BIT) // 0x41
#define REX_PUSH_POP_R8_PREFIX  (REX_BASE_0x40_BIT | REX_B_BIT) // 0x41


// ModR/M Register Mappings (3-bit Reg field values)
// These map to the lower 3 bits of the GPR number (0-7)
#define MODRM_REG_RAX       0x00 // EAX, RAX
#define MODRM_REG_RCX       0x01 // ECX, RCX
#define MODRM_REG_RDX       0x02 // EDX, RDX
#define MODRM_REG_RBX       0x03 // EBX, RBX
#define MODRM_REG_RSP       0x04 // ESP, RSP
#define MODRM_REG_RBP       0x05 // EBP, RBP
#define MODRM_REG_RSI       0x06 // ESI, RSI
#define MODRM_REG_RDI       0x07 // EDI, RDI

// Extended GPR Codes (when used with REX.R or REX.B)
// These are the low 3 bits of the extended register (R8-R15)
#define MODRM_REG_R8_CODE   0x00 // R8d, R8
#define MODRM_REG_R9_CODE   0x01 // R9d, R9
#define MODRM_REG_R10_CODE  0x02 // R10d, R10
#define MODRM_REG_R11_CODE  0x03 // R11d, R11
#define MODRM_REG_R12_CODE  0x04 // R12d, R12
#define MODRM_REG_R13_CODE  0x05 // R13d, R13
#define MODRM_REG_R14_CODE  0x06 // R14d, R14
#define MODRM_REG_R15_CODE  0x07 // R15d, R15

// XMM Registers (same 3-bit pattern as GPRs)
#define MODRM_REG_XMM0_CODE 0x00
#define MODRM_REG_XMM1_CODE 0x01
#define MODRM_REG_XMM2_CODE 0x02
#define MODRM_REG_XMM3_CODE 0x03
#define MODRM_REG_XMM4_CODE 0x04
#define MODRM_REG_XMM5_CODE 0x05
#define MODRM_REG_XMM6_CODE 0x06
#define MODRM_REG_XMM7_CODE 0x07

// ModR/M Mod field (2 bits)
#define MOD_INDIRECT        0x00 // Memory mode, no displacement (unless R/M is 0x05 or SIB follows)
#define MOD_DISP8           0x01 // Memory mode, 8-bit displacement
#define MOD_DISP32          0x02 // Memory mode, 32-bit displacement
#define MOD_REGISTER        0x03 // Register mode (operand is a register)

// ModR/M R/M field (3 bits)
#define RM_RBP_DISP32_OR_RIP 0x05 // For RBP + disp32 or RIP-relative (Mod=00)
#define RM_SIB_BYTE_FOLLOWS  0x04 // Indicates a SIB byte follows

// SIB Byte Components (Scale, Index, Base)
#define SIB_SCALE_1X        0x00
#define SIB_INDEX_NONE      0x04 // Indicates no index register (use ESP/RSP's encoding here)
#define SIB_BASE_R13        0x05 // R13 (Original, now unused for scratch)
#define SIB_BASE_R12        0x04 // R12 (Original, now unused for return buffer)
#define SIB_BASE_R14        0x06 // R14 (Used for args array base)
#define SIB_BASE_RCX        0x01 // RCX (Original scratch register for value_ptr, now unused)
#define SIB_BASE_R8         0x00 // R8 (Used for return_buffer_ptr)
#define SIB_BASE_R10        0x02 // R10 (New scratch register for value_ptr)
#define SIB_BASE_RSP        0x04 // RSP (Used as base for stack arguments)

// Pre-calculated SIB bytes for common indirect addressing
#define SIB_BYTE_R13_BASE   ((SIB_SCALE_1X << 6) | (SIB_INDEX_NONE << 3) | SIB_BASE_R13) // 0x25
#define SIB_BYTE_R12_BASE   ((SIB_SCALE_1X << 6) | (SIB_INDEX_NONE << 3) | SIB_BASE_R12) // 0x24
#define SIB_BYTE_R14_BASE   ((SIB_SCALE_1X << 6) | (SIB_INDEX_NONE << 3) | SIB_BASE_R14) // 0x26
#define SIB_BYTE_RCX_BASE   ((SIB_SCALE_1X << 6) | (SIB_INDEX_NONE << 3) | SIB_BASE_RCX) // 0x21
#define SIB_BYTE_R8_BASE    ((SIB_SCALE_1X << 6) | (SIB_INDEX_NONE << 3) | SIB_BASE_R8)  // 0x20
#define SIB_BYTE_R10_BASE   ((SIB_SCALE_1X << 6) | (SIB_INDEX_NONE << 3) | SIB_BASE_R10) // 0x22
#define SIB_BYTE_RSP        ((SIB_SCALE_1X << 6) | (SIB_INDEX_NONE << 3) | MODRM_REG_RSP) // 0x24 (Scale=1x, Index=None, Base=RSP)
#endif // FFI_ARCH_X64

// --- Assembly Instruction Component Defines (ARM64 AAPCS) ---
#ifdef FFI_ARCH_ARM64
// Instruction encodings (simplified for common patterns)
// These are placeholders and would need to be replaced with actual ARM64 instruction bytes.
// ARM64 instructions are 4 bytes long and fixed-width.
// This is a *highly simplified* representation for demonstration.
// A real implementation would require detailed knowledge of ARM64 instruction encoding.

// Example: MOV Xd, #imm (simplified)
#define ARM64_MOV_X_IMM(Xd, imm) (0xD2800000 | ((imm & 0xFFFF) << 5) | (Xd & 0x1F)) // movz/movk

// Example: LDR Xd, [Xn, #offset] (simplified)
#define ARM64_LDR_X_X_IMM(Xd, Xn, offset) (0xF8400000 | ((offset & 0xFFF) << 10) | ((Xn & 0x1F) << 5) | (Xd & 0x1F))

// Example: STR Xd, [Xn, #offset] (simplified)
#define ARM64_STR_X_X_IMM(Xd, Xn, offset) (0xF8000000 | ((offset & 0xFFF) << 10) | ((Xn & 0x1F) << 5) | (Xd & 0x1F))

// Example: BL (Branch with Link) to a register (simplified)
#define ARM64_BLR(Xn) (0xD63F0000 | ((Xn & 0x1F) << 5))

// Example: RET (simplified)
#define ARM64_RET() (0xD65F03C0)

// Example: STP Xm, Xn, [SP, #offset]! (pre-index) (simplified)
#define ARM64_STP_PRE_INDEX(Xm, Xn, offset) (0xA9800000 | ((offset & 0x7F) << 15) | ((Xn & 0x1F) << 10) | ((Xm & 0x1F) << 0))

// Example: LDP Xm, Xn, [SP], #offset (post-index) (simplified)
#define ARM64_LDP_POST_INDEX(Xm, Xn, offset) (0xA8C00000 | ((offset & 0x7F) << 15) | ((Xn & 0x1F) << 10) | ((Xm & 0x1F) << 0))

// Example: FMOV (float/double to general purpose register)
// FMOV Dd, Xn (double to GPR) - 0x9E6A0000 | (Xn << 5) | Dd
// FMOV Sd, Wn (float to GPR) - 0x1E620000 | (Wn << 5) | Sd
// FMOV Xn, Dd (GPR to double) - 0x9E6E0000 | (Dd << 5) | Xn
// FMOV Wn, Sd (GPR to float) - 0x1E660000 | (Sd << 5) | Wn

// Register numbers
#define ARM64_REG_X0  0
#define ARM64_REG_X1  1
#define ARM64_REG_X2  2
#define ARM64_REG_X3  3
#define ARM64_REG_X4  4
#define ARM64_REG_X5  5
#define ARM64_REG_X6  6
#define ARM64_REG_X7  7
#define ARM64_REG_X8  8 // Indirect result location register
#define ARM64_REG_SP  31 // Stack Pointer

#define ARM64_REG_V0  0
#define ARM64_REG_V1  1
#define ARM64_REG_V2  2
#define ARM64_REG_V3  3
#define ARM64_REG_V4  4
#define ARM64_REG_V5  5
#define ARM64_REG_V6  6
#define ARM64_REG_V7  7
#endif // FFI_ARCH_ARM64


// --- Foreign Functions (Implementations) ---
// Example foreign function: adds two integers (minimal version)
int add_two_ints(int a, int b) {
    note("--- Inside add_two_ints function ---");
    note("Received a: %d, b: %d", a, b);
    return a + b;
}

// Example foreign function: prints a float and a double, returns nothing
void print_float_and_double(float f_val, double d_val) {
    note("--- Inside print_float_and_double function ---");
    note("Received float: %f, double: %lf", f_val, d_val);
}

// Function: no arguments, returns void
void void_no_args_func() {
    note("--- Inside void_no_args_func function ---");
    note("Hello from dynamically invoked C function with no arguments!");
}

// Function: no arguments, returns an int
int get_fixed_int() {
    note("--- Inside get_fixed_int function ---");
    note("Returning fixed integer 42.");
    return 42;
}

// Minimal function: no arguments, returns an int (no printf calls)
int get_fixed_int_minimal() {
    return 42;
}

// Minimal function: no arguments, returns an int (used by manual main-like function)
int get_int_minimal() {
    return 42;
}

// NEW: Function: no arguments, returns a float
float get_float_value() {
    note("--- Inside get_float_value function ---");
    note("get_float_value: Returning 123.45f.");
    return 123.45f;
}

// NEW: Function: no arguments, returns a double
double get_double_value() {
    note("--- Inside get_double_value function ---");
    note("get_double_value: Returning 987.654.");
    return 987.654;
}

// Function: takes an int, returns an int (identity function)
int int_identity(int val) {
    note("--- Inside int_identity function ---");
    note("Received int: %d. Returning it back.", val);
    return val;
}

// NEW: Minimal function: takes an int, returns an int (no printf calls)
int int_identity_minimal(int val) {
    return val;
}

// NEW: Minimal function: takes a bool, returns a bool
bool bool_identity_minimal(bool val) {
    return val;
}

// NEW: Minimal function: takes a char, returns a char
char char_identity_minimal(char val) {
    return val;
}

// NEW: Minimal function: takes an unsigned char, returns an unsigned char
unsigned char uchar_identity_minimal(unsigned char val) {
    return val;
}

// NEW: Minimal function: takes a short, returns a short
short short_identity_minimal(short val) {
    return val;
}

// NEW: Minimal function: takes an unsigned short, returns an unsigned short
unsigned short ushort_identity_minimal(unsigned short val) {
    return val;
}

// NEW: Minimal function: takes a long, returns a long
long long_identity_minimal(long val) {
    return val;
}

// NEW: Minimal function: takes an unsigned long, returns an unsigned long
unsigned long ulong_identity_minimal(unsigned long val) {
    return val;
}

// NEW: Minimal function: takes a long long, returns a long long
long long llong_identity_minimal(long long val) {
    return val;
}

// NEW: Minimal function: takes an unsigned long long, returns an unsigned long long
unsigned long long ullong_identity_minimal(unsigned long long val) {
    return val;
}

// NEW: Minimal function: takes a float, returns a float
float float_identity_minimal(float val) {
    return val;
}

// NEW: Minimal function: takes a double, returns a double
double double_identity_minimal(double val) {
    return val;
}

// NEW: Minimal function: takes a pointer, returns a pointer
void* pointer_identity_minimal(void* val) {
    return val;
}

// Function: takes two ints, returns void
void print_two_ints(int a, int b) {
    note("--- Inside print_two_ints function ---");
    note("Received two integers: %d and %d. Returning void.", a, b);
}

// NEW: Function to test stack argument passing (more than 6 GPR arguments)
int sum_seven_ints(int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
    note("--- Inside sum_seven_ints function ---");
    note("Received: %d, %d, %d, %d, %d, %d, %d", a1, a2, a3, a4, a5, a6, a7);
    return a1 + a2 + a3 + a4 + a5 + a6 + a7;
}

// NEW: Mixed argument target function: int, float, void*
int mixed_int_float_ptr_func(int i_val, float f_val, void* ptr_val) {
    note("--- Inside mixed_int_float_ptr_func ---");
    note("Received int: %d, float: %f, pointer: %p", i_val, f_val, ptr_val);
    // Simple operation for a return value
    return i_val + (int)f_val + (ptr_val != NULL ? 1 : 0);
}

// NEW: Mixed argument target function: double, char, int
double mixed_double_char_int_func(double d_val, char c_val, int i_val) {
    note("--- Inside mixed_double_char_int_func ---");
    note("Received double: %lf, char: %c, int: %d", d_val, c_val, i_val);
    // Simple operation for a return value
    return d_val + (double)c_val + (double)i_val;
}

// NEW: Function that takes 8 integers (forces 2 onto stack)
int sum_eight_ints(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
    note("--- Inside sum_eight_ints function ---");
    note("Received: %d, %d, %d, %d, %d, %d, %d, %d", a1, a2, a3, a4, a5, a6, a7, a8);
    return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
}

// NEW: Function that takes 9 doubles (forces 1 onto stack)
double sum_nine_doubles(double d1, double d2, double d3, double d4, double d5, double d6, double d7, double d8, double d9) {
    note("--- Inside sum_nine_doubles function ---");
    note("Received: %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf", d1, d2, d3, d4, d5, d6, d7, d8, d9);
    return d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8 + d9;
}

// NEW: Function with mixed GPR and XMM arguments, forcing both to spill
int mixed_gpr_xmm_stack_spill_func(
    int i1, int i2, int i3, int i4, int i5, int i6, // GPRs in registers
    float f1, float f2, float f3, float f4, float f5, float f6, float f7, float f8, // XMMs in registers
    int i7, // GPR on stack
    double d9 // XMM on stack
) {
    note("--- Inside mixed_gpr_xmm_stack_spill_func ---");
    note("GPRs: %d, %d, %d, %d, %d, %d", i1, i2, i3, i4, i5, i6);
    note("XMMs: %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f", f1, f2, f3, f4, f5, f6, f7, f8);
    note("Stack GPR: %d, Stack XMM: %.2lf", i7, d9);
    return i1 + i2 + i3 + i4 + i5 + i6 + (int)f1 + (int)f2 + (int)f3 + (int)f4 + (int)f5 + (int)f6 + (int)f7 + (int)f8 + i7 + (int)d9;
}


// NEW: Even more minimal target function, just returns a constant
// This function is similar to add_two_ints but does not use printf.
int return_constant_42(int a, int b) {
    (void)a; // Suppress unused parameter warning
    (void)b; // Suppress unused parameter warning
    return 42;
}

// NEW: wchar_t identity function
wchar_t wchar_t_identity_minimal(wchar_t val) {
    return val;
}

// NEW: size_t identity function
size_t size_t_identity_minimal(size_t val) {
    return val;
}

// NEW: Explicit signed char identity function
signed char schar_identity_minimal(signed char val) {
    return val;
}

// NEW: Explicit signed short identity function
signed short sshort_identity_minimal(signed short val) {
    return val;
}

// NEW: Explicit signed int identity function
signed int sint_identity_minimal(signed int val) {
    return val;
}

// NEW: Explicit signed long identity function
signed long slong_identity_minimal(signed long val) {
    return val;
}

// NEW: Explicit signed long long identity function
signed long long sllong_identity_minimal(signed long long val) {
    return val;
}

// NEW: 128-bit signed integer identity function
#if defined(__SIZEOF_INT128__) || defined(__GNUC__)
__int128 int128_identity_minimal(__int128 val) {
    return val;
}

// NEW: 128-bit unsigned integer identity function
unsigned __int128 uint128_identity_minimal(unsigned __int128 val) {
    return val;
}
#else
// Placeholder for compilers not supporting __int128
// On MSVC, this would typically be a struct with two 64-bit integers.
// For simplicity, we'll just return a fixed value if __int128 is not supported.
typedef struct { uint64_t low; int64_t high; } int128_struct;
typedef struct { uint64_t low; uint64_t high; } uint128_struct;

int128_struct int128_identity_minimal(int128_struct val) {
    diag("Warning: __int128 not supported, using placeholder struct for int128_identity_minimal.");
    return val; // Return original value as best effort
}
uint128_struct uint128_identity_minimal(uint128_struct val) {
    diag("Warning: __int128 not supported, using placeholder struct for uint128_identity_minimal.");
    return val; // Return original value as best effort
}
#endif


// --- Runtime Assembly Generation and Execution Functions (Platform Agnostic) ---

/**
 * @brief Abstracts platform-specific memory allocation for executable memory.
 * @param size The number of bytes to allocate.
 * @return A pointer to the allocated memory, or NULL on failure.
 */
void* ffi_create_executable_memory(size_t size) {
#ifdef FFI_OS_LINUX
    long page_size_long = sysconf(_SC_PAGESIZE);
    size_t page_size = (size_t)page_size_long;
    size_t aligned_size = (size + page_size - 1) & ~(page_size - 1);
    void* mem = mmap(NULL, aligned_size,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap failed");
        BAIL_OUT("Failed to allocate executable memory with mmap.");
    }
    diag("Allocated executable memory at %p (size: %zu bytes) using mmap.", mem, aligned_size);
    return mem;
#elif defined(FFI_OS_WIN64)
    // VirtualAlloc allocates memory on page boundaries already
    void* mem = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (mem == NULL) {
        diag("VirtualAlloc failed with error: %lu", GetLastError());
        BAIL_OUT("Failed to allocate executable memory with VirtualAlloc.");
    }
    diag("Allocated executable memory at %p (size: %zu bytes) using VirtualAlloc.", mem, size);
    return mem;
#elif defined(FFI_OS_MACOS)
    long page_size_long = sysconf(_SC_PAGESIZE);
    size_t page_size = (size_t)page_size_long;
    size_t aligned_size = (size + page_size - 1) & ~(page_size - 1);
    void* mem = mmap(NULL, aligned_size,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap failed");
        BAIL_OUT("Failed to allocate executable memory with mmap.");
    }
    diag("Allocated executable memory at %p (size: %zu bytes) using mmap (macOS).", mem, aligned_size);
    return mem;
#else
    BAIL_OUT("ffi_create_executable_memory: Unsupported OS.");
    return NULL;
#endif
}

/**
 * @brief Abstracts platform-specific memory deallocation for executable memory.
 * @param mem Pointer to the memory to free.
 * @param size The size of the allocated memory.
 */
void ffi_free_executable_memory(void* mem, size_t size) {
    if (mem) {
#ifdef FFI_OS_LINUX
        long page_size_long = sysconf(_SC_PAGESIZE);
        size_t page_size = (size_t)page_size_long;
        size_t aligned_size = (size + page_size - 1) & ~(page_size - 1);
        if (munmap(mem, aligned_size) == -1) {
            perror("munmap failed");
            diag("WARNING: Failed to free executable memory at %p (Linux).", mem);
        } else {
            diag("Freed executable memory at %p (Linux).", mem);
        }
#elif defined(FFI_OS_WIN64)
        if (VirtualFree(mem, 0, MEM_RELEASE) == 0) {
            diag("VirtualFree failed with error: %lu", GetLastError());
            diag("WARNING: Failed to free executable memory at %p (Win64).", mem);
        } else {
            diag("Freed executable memory at %p (Win64).", mem);
        }
#elif defined(FFI_OS_MACOS)
        long page_size_long = sysconf(_SC_PAGESIZE);
        size_t page_size = (size_t)page_size_long;
        size_t aligned_size = (size + page_size - 1) & ~(page_size - 1);
        if (munmap(mem, aligned_size) == -1) {
            perror("munmap failed");
            diag("WARNING: Failed to free executable memory at %p (macOS).", mem);
        } else {
            diag("Freed executable memory at %p (macOS).", mem);
        }
#else
        diag("WARNING: ffi_free_executable_memory: Unsupported OS. Memory at %p not freed.", mem);
#endif
    }
}

/**
 * @brief Flushes the instruction cache for a given memory range.
 * This is crucial after writing executable code.
 * @param addr The start address of the memory range.
 * @param len The length of the memory range.
 */
void ffi_flush_instruction_cache(void* addr, size_t len) {
#if defined(__GNUC__) || defined(__clang__)
    // For GCC/Clang on Linux/macOS/ARM, __builtin___clear_cache is available.
    // It's a no-op on x86-64 as instruction cache coherency is handled by hardware.
    // But it's essential for ARM.
    __builtin___clear_cache((char*)addr, (char*)addr + len);
    diag("Instruction cache flushed for %p - %p (GCC/Clang builtin).", addr, (char*)addr + len);
#elif defined(FFI_OS_WIN64)
    // For Windows, use FlushInstructionCache
    if (!FlushInstructionCache(GetCurrentProcess(), addr, len)) {
        diag("WARNING: FlushInstructionCache failed with error: %lu", GetLastError());
    } else {
        diag("Instruction cache flushed for %p - %p (Win64).", addr, (char*)addr + len);
    }
#else
    diag("WARNING: Instruction cache flush not implemented for this platform.");
#endif
}


#ifdef FFI_ARCH_X64
/**
 * @brief Generates x86-64 System V ABI trampoline bytes.
 * @param code_buffer Pointer to the memory where the assembly bytes will be written.
 * @param sig A pointer to the FFI_FunctionSignature.
 * @return The size of the generated assembly code in bytes.
 */
size_t generate_x86_64_sysv_trampoline(unsigned char* code_buffer, FFI_FunctionSignature* sig) {
    unsigned char *current_code_ptr = code_buffer;
    long target_addr_val;

    // The trampoline itself will be called by C with this signature:
    // void (*GenericTrampoline)(FFI_Argument* args, int num_args, void* return_buffer_ptr)
    // Register mapping for this trampoline call (System V):
    // %rdi: FFI_Argument* args (base address of the FFI_Argument array)
    // %rsi: int num_args (number of arguments in the array)
    // %rdx: void* return_buffer_ptr (pointer to where the return value should be stored)

    // --- CET Compliance: endbr64 ---
    *current_code_ptr++ = 0xF3;
    *current_code_ptr++ = 0x0F;
    *current_code_ptr++ = 0x1E;
    *current_code_ptr++ = OPCODE_END_BRANCH_64; // 0xFA

    // --- Prologue ---
    // push %rbp
    *current_code_ptr++ = OPCODE_PUSH_RBP;

    // mov %rsp, %rbp
    *current_code_ptr++ = REX_W_PREFIX;
    *current_code_ptr++ = OPCODE_MOV_RM64_R64; // mov r/m64, r64
    *current_code_ptr++ = (MOD_REGISTER << 6) | (MODRM_REG_RSP << 3) | MODRM_REG_RBP;

    // Save callee-saved registers that we'll use: R12, R14
    // System V callee-saved: RBX, RBP, R12, R13, R14, R15
    // We are using R14 (for args_ptr) and R12 (for return_buffer_ptr) as callee-saved.
    // R10 and R11 are caller-saved, so we don't need to save/restore them unless we need their values across calls.
    // In this trampoline, R10 and R11 are scratch, so no explicit save/restore for them.
    // R14 (args pointer from rdi)
    *current_code_ptr++ = REX_PUSH_POP_R14_PREFIX; // 0x41
    *current_code_ptr++ = OPCODE_PUSH_R14_BYTE;    // 0x56

    // mov %rdi, %r14 (Save args pointer from rdi to r14 for later use in argument marshalling)
    *current_code_ptr++ = REX_WB_PREFIX; // 0x49 (W=1, B=1 for R14)
    *current_code_ptr++ = OPCODE_MOV_RM64_R64; // mov r/m64, r64 (store rdi to r14)
    *current_code_ptr++ = (MOD_REGISTER << 6) | (MODRM_REG_RDI << 3) | MODRM_REG_R14_CODE; // 0xCE

    // R12 (return_buffer_ptr from rdx)
    *current_code_ptr++ = REX_PUSH_POP_R12_PREFIX; // 0x41
    *current_code_ptr++ = OPCODE_PUSH_R12_BYTE;    // 0x54

    // mov %rdx, %r12 (Save return_buffer_ptr from rdx to r12)
    *current_code_ptr++ = REX_WB_PREFIX; // 0x49 (W=1, B=1 for R12)
    *current_code_ptr++ = OPCODE_MOV_RM64_R64; // mov r/m64, r64 (store rdx to r12)
    *current_code_ptr++ = (MOD_REGISTER << 6) | (MODRM_REG_RDX << 3) | MODRM_REG_R12_CODE; // 0xEA

    // --- Determine Stack Arguments and Calculate Total Stack Space ---
    int num_gp_regs_used = 0;
    int num_xmm_regs_used = 0;
    int num_stack_args = 0; // Number of 8-byte slots needed on stack

    for (int i = 0; i < sig->num_params; ++i) {
        FFI_Type param_type = sig->param_types[i];
        if (param_type == FFI_TYPE_FLOAT || param_type == FFI_TYPE_DOUBLE) {
            if (num_xmm_regs_used < 8) { // System V has 8 XMM registers (XMM0-XMM7)
                num_xmm_regs_used++;
            } else {
                num_stack_args++;
            }
        } else if (param_type == FFI_TYPE_INT128 || param_type == FFI_TYPE_UINT128) {
            // 128-bit integers consume two GPRs or 16 bytes on stack
            // System V GPRs: RDI, RSI, RDX, RCX, R8, R9 (6 registers)
            if (num_gp_regs_used <= 4) { // If num_gp_regs_used is 5, we only have R9 left, so it spills.
                num_gp_regs_used += 2; // Consumes two GPRs
            } else {
                num_stack_args += 2; // Consumes 16 bytes on stack
            }
        } else { // All other types (integers, bool, char, pointer, wchar_t, size_t) go to GPRs
            if (num_gp_regs_used < 6) { // System V has 6 GPRs
                num_gp_regs_used++;
            } else {
                num_stack_args++;
            }
        }
    }

    size_t final_stack_subtraction = 0;

    // Calculate the total size needed for stack arguments.
    size_t stack_args_total_size = (size_t)num_stack_args * 8;

    // After push RBP, push R14, push R12, RSP is 24 bytes lower than initial (16-aligned) RSP.
    // So, RSP is currently 8-byte aligned.
    // We need RSP to be 8-byte aligned BEFORE the CALL so that after CALL pushes 8 bytes,
    // RSP is 16-byte aligned inside the callee.
    // This means final_stack_subtraction must be a multiple of 16.
  if (num_stack_args > 0) {
        final_stack_subtraction = stack_args_total_size;
        // Ensure final_stack_subtraction is a multiple of 16
        while ((final_stack_subtraction % 16) != 0) {
            final_stack_subtraction++;
        }
    } else { // No stack arguments
        // RSP is already 8-byte aligned after pushes. No need to subtract more.
        final_stack_subtraction = 0;
    }
    // The 32-byte shadow space (red zone) is implicitly available and not allocated by the caller.

    if (final_stack_subtraction > 0) {
        *current_code_ptr++ = REX_W_PREFIX; // REX.W prefix for 64-bit operation
        *current_code_ptr++ = OPCODE_SUB_IMM8_RSP; // 0x83 (SUB r/m64, imm8)
        *current_code_ptr++ = (MOD_REGISTER << 6) | (0x05 << 3) | MODRM_REG_RSP; // Mod=11, Reg=Group 5 (SUB), R/M=RSP (0x04) -> 0xEC
        *current_code_ptr++ = (unsigned char)final_stack_subtraction; // imm8
    }

    // IMPORTANT: Ensure AL is set to 0 for non-variadic functions (ABI compliance).
    // This must be done regardless of stack arguments.
    *current_code_ptr++ = 0xB0; // MOV AL, imm8
    *current_code_ptr++ = 0x00; // imm8 = 0


    // --- Argument Marshalling ---
    int gp_reg_idx = 0;
    int xmm_reg_idx = 0;
    int stack_arg_current_idx = 0; // 0-indexed counter for arguments going to stack

    // Array of GPR argument registers in order: RDI, RSI, RDX, RCX, R8, R9
    unsigned char gp_arg_regs[] = { MODRM_REG_RDI, MODRM_REG_RSI, MODRM_REG_RDX, MODRM_REG_RCX, MODRM_REG_R8_CODE, MODRM_REG_R9_CODE };
    bool gp_arg_regs_needs_rex_r[] = { false, false, false, false, true, true }; // R8, R9 need REX.R

    if (sig->num_params > 0 && sig->param_types != NULL) {
        for (int i = 0; i < sig->num_params; ++i) {
            FFI_Type param_type = sig->param_types[i];

            // Load args[i].value_ptr into R10 (temporary register for base address)
            size_t current_arg_value_ptr_offset = (size_t)i * sizeof(FFI_Argument);
            *current_code_ptr++ = REX_WR_PREFIX | REX_B_BIT; // 0x4D (W=1, R=1, B=1)
            *current_code_ptr++ = OPCODE_MOV_R64_RM64; // mov r64, r/m64 (LOAD from memory)
            *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_R10_CODE << 3) | MODRM_REG_R14_CODE); // Mod=01, Reg=R10, R/M=R14_CODE
            *current_code_ptr++ = (unsigned char)current_arg_value_ptr_offset; // disp8

            bool is_current_param_xmm_type = (param_type == FFI_TYPE_FLOAT || param_type == FFI_TYPE_DOUBLE);
            bool is_current_param_int128_type = (param_type == FFI_TYPE_INT128 || param_type == FFI_TYPE_UINT128);
            bool goes_to_reg = false;
            bool to_stack = false;

            if (is_current_param_xmm_type) {
                if (xmm_reg_idx < 8) { // 8 XMM registers for System V
                    goes_to_reg = true;
                    // MOVSS/MOVSD XMMn, [R10]
                    unsigned char xmm_prefix = (param_type == FFI_TYPE_FLOAT) ? PREFIX_MOVSS : PREFIX_MOVSD;
                    unsigned char xmm_rex_prefix = REX_BASE_0x40_BIT | REX_B_BIT; // REX.B for R10 as base
                    // XMM0-XMM7 do not need REX.R bit.
                    *current_code_ptr++ = xmm_prefix;
                    *current_code_ptr++ = xmm_rex_prefix;
                    *current_code_ptr++ = 0x0F;
                    *current_code_ptr++ = OPCODE_XMM_MOV_XMM_RM; // 0x10
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_XMM0_CODE + xmm_reg_idx << 3) | MODRM_REG_R10_CODE);
                    xmm_reg_idx++;
                } else {
                    to_stack = true;
                }
            } else if (is_current_param_int128_type) {
                // 128-bit integers need two GPRs
                if (gp_reg_idx <= 4) { // Check if there are at least two registers remaining (R9 is gp_reg_idx 5)
                    goes_to_reg = true;
                    // Load lower 64 bits from [R10] into first GPR
                    unsigned char dest_reg_low = gp_arg_regs[gp_reg_idx];
                    bool needs_rex_r_low = gp_arg_regs_needs_rex_r[gp_reg_idx];
                    unsigned char rex_prefix_low = REX_W_PREFIX | REX_B_BIT;
                    if (needs_rex_r_low) rex_prefix_low |= REX_R_BIT;
                    *current_code_ptr++ = rex_prefix_low;
                    *current_code_ptr++ = OPCODE_MOV_R64_RM64; // MOV reg, [mem]
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (dest_reg_low << 3) | MODRM_REG_R10_CODE);

                    // Load upper 64 bits from [R10 + 8] into second GPR
                    unsigned char dest_reg_high = gp_arg_regs[gp_reg_idx + 1];
                    bool needs_rex_r_high = gp_arg_regs_needs_rex_r[gp_reg_idx + 1];
                    unsigned char rex_prefix_high = REX_W_PREFIX | REX_B_BIT;
                    if (needs_rex_r_high) rex_prefix_high |= REX_R_BIT;
                    *current_code_ptr++ = rex_prefix_high;
                    *current_code_ptr++ = OPCODE_MOV_R64_RM64; // MOV reg, [mem]
                    *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (dest_reg_high << 3) | MODRM_REG_R10_CODE);
                    *current_code_ptr++ = 0x08; // disp8 = 8 (for upper 64 bits)

                    gp_reg_idx += 2; // Consume two GPRs
                } else {
                    to_stack = true;
                }
            } else { // All other types (integers, bool, char, pointer, wchar_t, size_t)
                if (gp_reg_idx < 6) { // 6 GPRs for System V
                    goes_to_reg = true;
                    unsigned char dest_reg_code = gp_arg_regs[gp_reg_idx];
                    bool use_rex_r_for_dest_reg = gp_arg_regs_needs_rex_r[gp_reg_idx];
                    unsigned char current_opcode;
                    unsigned char modrm = (unsigned char)((MOD_INDIRECT << 6) | (dest_reg_code << 3) | MODRM_REG_R10_CODE);
                    unsigned char final_rex_prefix = REX_BASE_0x40_BIT | REX_B_BIT;
                    if (use_rex_r_for_dest_reg) final_rex_prefix |= REX_R_BIT;

                    switch (param_type) {
                        case FFI_TYPE_BOOL:
                        case FFI_TYPE_CHAR:
                        case FFI_TYPE_UCHAR:
                            current_opcode = 0xB6; // MOVZX r64, r/m8
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_SCHAR:
                            current_opcode = 0xBE; // MOVSX r64, r/m8
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_SHORT:
                        case FFI_TYPE_SSHORT:
                            current_opcode = 0xBF; // MOVSX r64, r/m16
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = 0x66; *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_USHORT:
                            current_opcode = 0xB7; // MOVZX r64, r/m16
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = 0x66; *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_INT:
                        case FFI_TYPE_SINT:
                        case FFI_TYPE_WCHAR:
                            current_opcode = 0x63; // MOVSXD r64, r/m32
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_UINT:
                            current_opcode = OPCODE_MOV_R64_RM64; // MOV r32, r/m32 (zero-extends)
                            *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_LONG:
                        case FFI_TYPE_ULONG:
                        case FFI_TYPE_LLONG:
                        case FFI_TYPE_ULLONG:
                        case FFI_TYPE_POINTER:
                        case FFI_TYPE_SIZE_T:    // size_t is typically 64-bit on x86-64
                        case FFI_TYPE_SLONG:
                        case FFI_TYPE_SLLONG:
                            current_opcode = OPCODE_MOV_R64_RM64; // MOV r64, r/m64
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        default: return 0; // Error
                    }
                    gp_reg_idx++;
                } else {
                    to_stack = true;
                }
            }

            if (to_stack) {
                if (is_current_param_int128_type) {
                    // Corrected 128-bit to stack: Load both halves into scratch registers first
                    // Load lower 64 bits into R11
                    *current_code_ptr++ = REX_WR_PREFIX | REX_B_BIT; // REX.W for 64-bit, REX.R for R11, REX.B for R10
                    *current_code_ptr++ = OPCODE_MOV_R64_RM64; // MOV R11, [R10]
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_R11_CODE << 3) | MODRM_REG_R10_CODE);

                    // Load upper 64 bits into R13
                    *current_code_ptr++ = REX_PUSH_POP_R13_PREFIX | REX_W_PREFIX | REX_R_BIT; // REX.B for R10, REX.R for R13
                    *current_code_ptr++ = OPCODE_MOV_R64_RM64; // MOV R13, [R10 + 8]
                    *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_R13_CODE << 3) | MODRM_REG_R10_CODE);
                    *current_code_ptr++ = 0x08; // disp8 = 8

                    // Store R11 to [RSP + offset_low]
                    size_t stack_offset_low = (size_t)stack_arg_current_idx * 8;
                    *current_code_ptr++ = REX_W_PREFIX | REX_R_BIT; // REX.W for 64-bit, REX.R for R11
                    *current_code_ptr++ = OPCODE_MOV_RM64_R64; // MOV [RSP + offset_low], R11
                    *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_R11_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_RSP;
                    *current_code_ptr++ = (unsigned char)stack_offset_low;

                    // Store R13 to [RSP + offset_high]
                    size_t stack_offset_high = (size_t)(stack_arg_current_idx + 1) * 8;
                    *current_code_ptr++ = REX_W_PREFIX | REX_R_BIT; // REX.W for 64-bit, REX.R for R13
                    *current_code_ptr++ = OPCODE_MOV_RM64_R64; // MOV [RSP + offset_high], R13
                    *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_R13_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_RSP;
                    *current_code_ptr++ = (unsigned char)stack_offset_high;

                    stack_arg_current_idx += 2; // Consumes two stack slots
                } else if (is_current_param_xmm_type) {
                    // Load float/double from (R10) into XMM7 (scratch register, MODRM_REG_XMM7_CODE)
                    unsigned char xmm_prefix = (param_type == FFI_TYPE_FLOAT) ? PREFIX_MOVSS : PREFIX_MOVSD;
                    *current_code_ptr++ = xmm_prefix;
                    *current_code_ptr++ = (REX_BASE_0x40_BIT | REX_R_BIT | REX_B_BIT); // REX.R for XMM7
                    *current_code_ptr++ = 0x0F;
                    *current_code_ptr++ = OPCODE_XMM_MOV_XMM_RM; // 0x10
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_XMM7_CODE << 3) | MODRM_REG_R10_CODE);

                    // Store float/double from XMM7 to (RSP + stack_offset_from_rsp_base)
                    size_t stack_offset_from_rsp_base = (size_t)stack_arg_current_idx * 8;
                    *current_code_ptr++ = xmm_prefix;
                    *current_code_ptr++ = (REX_BASE_0x40_BIT | REX_R_BIT); // REX.R for XMM7
                    *current_code_ptr++ = 0x0F;
                    *current_code_ptr++ = OPCODE_XMM_MOV_RM_XMM; // 0x11
                    *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_XMM7_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_RSP;
                    *current_code_ptr++ = (unsigned char)stack_offset_from_rsp_base;
                    stack_arg_current_idx++;
                } else { // GPR types to stack
                    size_t stack_offset_from_rsp_base = (size_t)stack_arg_current_idx * 8;
                    // Load the value from (R10) into R11 (temporary register)
                    unsigned char load_rex_prefix = REX_BASE_0x40_BIT | REX_B_BIT | REX_R_BIT; // R10 is base, R11 is dest (R11's code is 0x03, needs REX.R)
                    unsigned char load_opcode;
                    unsigned char load_modrm = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_R11_CODE << 3) | MODRM_REG_R10_CODE);

                    switch (param_type) {
                        case FFI_TYPE_BOOL: case FFI_TYPE_CHAR: case FFI_TYPE_UCHAR:
                            load_opcode = 0xB6; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_SCHAR:
                            load_opcode = 0xBE; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_SHORT: case FFI_TYPE_SSHORT:
                            load_opcode = 0xBF; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = 0x66; *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_USHORT:
                            load_opcode = 0xB7; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = 0x66; *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_INT: case FFI_TYPE_SINT: case FFI_TYPE_WCHAR:
                            load_opcode = 0x63; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_UINT:
                            load_opcode = OPCODE_MOV_R64_RM64;
                            *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_LONG: case FFI_TYPE_ULONG: case FFI_TYPE_LLONG: case FFI_TYPE_ULLONG:
                        case FFI_TYPE_POINTER: case FFI_TYPE_SIZE_T: case FFI_TYPE_SLONG: case FFI_TYPE_SLLONG:
                            load_opcode = OPCODE_MOV_R64_RM64; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        default: return 0; // Error
                    }

                    // Step 2: Store the value from R11 to (RSP + stack_offset_from_rsp_base)
                    unsigned char store_rex_prefix = REX_W_PREFIX | REX_R_BIT;
                    *current_code_ptr++ = store_rex_prefix;
                    *current_code_ptr++ = OPCODE_MOV_RM64_R64;
                    unsigned char store_modrm_mod = (stack_offset_from_rsp_base == 0) ? MOD_INDIRECT : MOD_DISP8;
                    *current_code_ptr++ = (unsigned char)((store_modrm_mod << 6) | (MODRM_REG_R11_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_RSP;
                    if (stack_offset_from_rsp_base != 0) {
                        *current_code_ptr++ = (unsigned char)stack_offset_from_rsp_base;
                    }
                    stack_arg_current_idx++;
                }
            }
        }
    }


    // --- Call Target Function ---
    // movabs RAX, <target_func_address>
    // Write REX.W prefix (0x48)
    *current_code_ptr++ = REX_W_PREFIX;
    // Write MOV RAX, imm64 opcode (0xB8)
    *current_code_ptr++ = OPCODE_MOV_IMM64_RAX;

    target_addr_val = (long)(uintptr_t)sig->func_ptr; // Get the 64-bit address as a long (cast through uintptr_t)
    // Write the 8-byte target function address
    memcpy(current_code_ptr, &target_addr_val, 8);
    current_code_ptr += 8;

    // call RAX
    *current_code_ptr++ = OPCODE_CALL_RM64; // CALL r/m64
    *current_code_ptr++ = (unsigned char)((MOD_REGISTER << 6) | (0x02 << 3) | MODRM_REG_RAX);

    // --- Return Value Handling ---
    // Store return value (from EAX/RAX or XMM0) into (R12)
    if (sig->return_type != FFI_TYPE_VOID) {
        switch (sig->return_type) {
            case FFI_TYPE_BOOL:
            case FFI_TYPE_CHAR:
            case FFI_TYPE_UCHAR:
            case FFI_TYPE_SCHAR:
                // movb AL, [R12]
                *current_code_ptr++ = REX_B_PREFIX_32BIT_OP; // 0x41 (for R12 as base)
                *current_code_ptr++ = 0x88; // MOV r/m8, r8 (AL is 8-bit part of RAX)
                *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_RAX << 3) | RM_SIB_BYTE_FOLLOWS);
                *current_code_ptr++ = SIB_BYTE_R12_BASE;
                break;
            case FFI_TYPE_SHORT:
            case FFI_TYPE_USHORT:
            case FFI_TYPE_SSHORT:
                // movw AX, [R12]
                *current_code_ptr++ = 0x66; // Operand-size override prefix for 16-bit
                *current_code_ptr++ = REX_B_PREFIX_32BIT_OP; // 0x41 (for R12 as base)
                *current_code_ptr++ = 0x89; // MOV r/m16, r16 (AX is 16-bit part of RAX)
                *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_RAX << 3) | RM_SIB_BYTE_FOLLOWS);
                *current_code_ptr++ = SIB_BYTE_R12_BASE;
                break;
            case FFI_TYPE_INT:
            case FFI_TYPE_UINT:
            case FFI_TYPE_SINT:
            case FFI_TYPE_WCHAR: // wchar_t is typically 32-bit on Linux
                // movl EAX, [R12]
                *current_code_ptr++ = REX_B_PREFIX_32BIT_OP; // 0x41
                *current_code_ptr++ = OPCODE_MOV_RM64_R64;   // 0x89
                *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_RAX << 3) | RM_SIB_BYTE_FOLLOWS);
                *current_code_ptr++ = SIB_BYTE_R12_BASE;
                break;
            case FFI_TYPE_LONG:
            case FFI_TYPE_ULONG:
            case FFI_TYPE_LLONG:
            case FFI_TYPE_ULLONG:
            case FFI_TYPE_POINTER:
            case FFI_TYPE_SIZE_T: // size_t is typically 64-bit on x86-64
            case FFI_TYPE_SLONG:
            case FFI_TYPE_SLLONG:
                // movq RAX, [R12]
                *current_code_ptr++ = REX_WB_PREFIX;         // 0x49
                *current_code_ptr++ = OPCODE_MOV_RM64_R64;   // 0x89
                *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_RAX << 3) | RM_SIB_BYTE_FOLLOWS);
                *current_code_ptr++ = SIB_BYTE_R12_BASE;
                break;
            case FFI_TYPE_FLOAT:
                // movss XMM0, [R12]
                *current_code_ptr++ = PREFIX_MOVSS;          // 0xF3
                *current_code_ptr++ = (REX_BASE_0x40_BIT | REX_B_BIT); // 0x41
                *current_code_ptr++ = 0x0F;
                *current_code_ptr++ = OPCODE_XMM_MOV_RM_XMM; // 0x11
                *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_XMM0_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                *current_code_ptr++ = SIB_BYTE_R12_BASE;
                break;
            case FFI_TYPE_DOUBLE:
                // movsd XMM0, [R12]
                *current_code_ptr++ = PREFIX_MOVSD;          // 0xF2
                *current_code_ptr++ = (REX_BASE_0x40_BIT | REX_B_BIT); // 0x41
                *current_code_ptr++ = 0x0F;
                *current_code_ptr++ = OPCODE_XMM_MOV_RM_XMM; // 0x11
                *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_XMM0_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                *current_code_ptr++ = SIB_BYTE_R12_BASE;
                break;
            case FFI_TYPE_INT128:
            case FFI_TYPE_UINT128:
                // Return value in RDX:RAX (RAX is lower 64, RDX is upper 64)
                // Store RAX (lower) to [R12]
                *current_code_ptr++ = REX_WB_PREFIX; // 0x49
                *current_code_ptr++ = OPCODE_MOV_RM64_R64; // 0x89
                *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_RAX << 3) | RM_SIB_BYTE_FOLLOWS);
                *current_code_ptr++ = SIB_BYTE_R12_BASE;

                // Store RDX (upper) to [R12 + 8]
                *current_code_ptr++ = REX_WB_PREFIX; // 0x49
                *current_code_ptr++ = OPCODE_MOV_RM64_R64; // 0x89
                *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_RDX << 3) | RM_SIB_BYTE_FOLLOWS);
                *current_code_ptr++ = SIB_BYTE_R12_BASE;
                *current_code_ptr++ = 0x08; // disp8 = 8
                break;
            default: // This default case catches FFI_TYPE_UNKNOWN or any other unsupported type for return
                return 0; // Indicate error
        }
    }

    // --- Epilogue ---
    // Reverse stack alignment only if space was allocated
    if (final_stack_subtraction > 0) {
        *current_code_ptr++ = REX_W_PREFIX; // REX.W prefix for 64-bit operation
        *current_code_ptr++ = OPCODE_ADD_IMM8_RSP; // 0x83 (ADD r/m64, imm8)
        *current_code_ptr++ = (MOD_REGISTER << 6) | (0x00 << 3) | MODRM_REG_RSP; // Mod=11, Reg=Group 0 (ADD), R/M=RSP (0x04) -> 0xC4
        *current_code_ptr++ = (unsigned char)final_stack_subtraction; // imm8
    }

    // Pop R12 to restore its original value
    *current_code_ptr++ = REX_PUSH_POP_R12_PREFIX; // 0x41
    *current_code_ptr++ = OPCODE_POP_R12_BYTE;     // 0x5C

    // Pop R14 to restore original RDI (args pointer)
    *current_code_ptr++ = REX_PUSH_POP_R14_PREFIX;
    *current_code_ptr++ = OPCODE_POP_R14_BYTE;

    // pop RBP
    *current_code_ptr++ = OPCODE_POP_RBP;

    // ret
    *current_code_ptr++ = OPCODE_RET;

    return (size_t)(current_code_ptr - code_buffer);
}

/**
 * @brief Generates x86-64 Microsoft x64 ABI trampoline bytes (Win64).
 * @param code_buffer Pointer to the memory where the assembly bytes will be written.
 * @param sig A pointer to the FFI_FunctionSignature.
 * @return The size of the generated assembly code in bytes.
 */
size_t generate_x86_64_win64_trampoline(unsigned char* code_buffer, FFI_FunctionSignature* sig) {
    unsigned char *current_code_ptr = code_buffer;
    long target_addr_val;

    // The trampoline itself will be called by C with this signature:
    // void (*GenericTrampoline)(FFI_Argument* args, int num_args, void* return_buffer_ptr)
    // Register mapping for this trampoline call (Win64):
    // %rcx: FFI_Argument* args (base address of the FFI_Argument array)
    // %rdx: int num_args (number of arguments in the array)
    // %r8:  void* return_buffer_ptr (pointer to where the return value should be stored)

    // --- Prologue ---
    // push %rbp
    *current_code_ptr++ = OPCODE_PUSH_RBP;

    // mov %rsp, %rbp
    *current_code_ptr++ = REX_W_PREFIX;
    *current_code_ptr++ = OPCODE_MOV_RM64_R64; // mov r/m64, r64
    *current_code_ptr++ = (MOD_REGISTER << 6) | (MODRM_REG_RSP << 3) | MODRM_REG_RBP;

    // Save callee-saved registers that we'll use: RBX, RDI, RSI, R12, R13, R14, R15
    // We need to save RCX (args), RDX (num_args), R8 (return_buffer_ptr) if we need them after pushing other args.
    // However, for simplicity, we'll just save R13 (for args_ptr) and R14 (for return_buffer_ptr)
    // and move the incoming RCX and R8 into them.
    // R13 (args pointer from rcx)
    *current_code_ptr++ = REX_PUSH_POP_R13_PREFIX; // 0x41
    *current_code_ptr++ = OPCODE_PUSH_R13_BYTE;    // 0x55 (REX.B for R13, 0x55 is PUSH RBP, but with REX.B it's PUSH R13)

    // mov %rcx, %r13 (Save args pointer from rcx to r13)
    *current_code_ptr++ = REX_WB_PREFIX; // 0x49 (W=1, B=1 for R13)
    *current_code_ptr++ = OPCODE_MOV_RM64_R64; // mov r/m64, r64 (store rcx to r13)
    *current_code_ptr++ = (MOD_REGISTER << 6) | (MODRM_REG_RCX << 3) | MODRM_REG_R13_CODE; // 0xCB

    // R14 (return_buffer_ptr from r8)
    *current_code_ptr++ = REX_PUSH_POP_R14_PREFIX; // 0x41
    *current_code_ptr++ = OPCODE_PUSH_R14_BYTE;    // 0x56

    // mov %r8, %r14 (Save return_buffer_ptr from r8 to r14)
    *current_code_ptr++ = REX_WR_PREFIX | REX_B_BIT; // 0x4D (W=1, R=1 for R8, B=1 for R14)
    *current_code_ptr++ = OPCODE_MOV_RM64_R64; // mov r/m64, r64 (store r8 to r14)
    *current_code_ptr++ = (MOD_REGISTER << 6) | (MODRM_REG_R8_CODE << 3) | MODRM_REG_R14_CODE; // 0xC6

    // --- Determine Stack Arguments and Calculate Total Stack Space ---
    int num_gp_regs_used = 0;
    int num_xmm_regs_used = 0;
    int num_stack_args = 0; // Number of 8-byte slots needed on stack

    // Win64 GPRs: RCX, RDX, R8, R9 (4 registers)
    // Win64 XMMs: XMM0, XMM1, XMM2, XMM3 (4 registers)

    // First, check if a return buffer pointer is needed for structs > 8 bytes.
    // If so, it consumes RCX (the first argument register)
    bool return_struct_by_pointer = false;
    if (sig->return_type == FFI_TYPE_INT128 || sig->return_type == FFI_TYPE_UINT128) {
        // On Win64, __int128 is typically returned by reference (pointer in RCX)
        return_struct_by_pointer = true;
        num_gp_regs_used++; // RCX is used for return buffer pointer
    } else if (sig->return_type == FFI_TYPE_UNKNOWN) {
        // Handle other struct types if they were added. For now, only 128-bit.
        // For structs > 8 bytes, Win64 returns by pointer.
        // This FFI doesn't currently support arbitrary structs, so we assume 128-bit is the only case.
    }

    for (int i = 0; i < sig->num_params; ++i) {
        FFI_Type param_type = sig->param_types[i];
        if (param_type == FFI_TYPE_FLOAT || param_type == FFI_TYPE_DOUBLE) {
            if (num_xmm_regs_used < 4) { // Win64 has 4 XMM registers
                num_xmm_regs_used++;
            } else {
                num_stack_args++;
            }
        } else if (param_type == FFI_TYPE_INT128 || param_type == FFI_TYPE_UINT128) {
            // 128-bit integers consume two GPRs or 16 bytes on stack
            // On Win64, 128-bit values are passed as two 64-bit arguments.
            if (num_gp_regs_used <= 2) { // If num_gp_regs_used is 3, we only have R9 left, so it spills.
                num_gp_regs_used += 2; // Consumes two GPRs
            } else {
                num_stack_args += 2; // Consumes 16 bytes on stack
            }
        } else { // All other types (integers, bool, char, pointer, wchar_t, size_t) go to GPRs
            if (num_gp_regs_used < 4) { // Win64 has 4 GPRs
                num_gp_regs_used++;
            } else {
                num_stack_args++;
            }
        }
    }

    // Win64 ABI requires 32 bytes of "shadow space" (for the first 4 arguments, even if not used)
    // plus space for any additional stack arguments.
    // The total stack space to subtract must be 16-byte aligned.
    size_t total_stack_alloc = 32; // Minimum 32 bytes for shadow space
    total_stack_alloc += (size_t)num_stack_args * 8; // Add space for spilled arguments

    // Ensure total_stack_alloc is 16-byte aligned.
    // RSP is 8-byte aligned after push RBP, push R13, push R14 (24 bytes pushed).
    // So current RSP is 8-byte aligned.
    // We need RSP to be 16-byte aligned *before* the CALL.
    // (24 + total_stack_alloc) % 16 == 0
    // (8 + total_stack_alloc) % 16 == 0
    if (((8 + total_stack_alloc) % 16) != 0) {
        total_stack_alloc += 8; // Add 8 bytes for alignment
    }

    if (total_stack_alloc > 0) {
        *current_code_ptr++ = REX_W_PREFIX; // REX.W prefix for 64-bit operation
        *current_code_ptr++ = OPCODE_SUB_IMM8_RSP; // 0x83 (SUB r/m64, imm8)
        *current_code_ptr++ = (MOD_REGISTER << 6) | (0x05 << 3) | MODRM_REG_RSP; // Mod=11, Reg=Group 5 (SUB), R/M=RSP (0x04) -> 0xEC
        *current_code_ptr++ = (unsigned char)total_stack_alloc; // imm8
    }

    // --- Argument Marshalling ---
    int gp_reg_idx = 0;
    int xmm_reg_idx = 0;
    int stack_arg_current_idx = 0; // 0-indexed counter for arguments going to stack

    // Array of GPR argument registers in order for Win64: RCX, RDX, R8, R9
    unsigned char gp_arg_regs[] = { MODRM_REG_RCX, MODRM_REG_RDX, MODRM_REG_R8_CODE, MODRM_REG_R9_CODE };
    bool gp_arg_regs_needs_rex_r[] = { false, false, true, true }; // R8, R9 need REX.R

    // If returning a struct by pointer, the return buffer pointer is the first argument (RCX)
    if (return_struct_by_pointer) {
        // mov %r14, %rcx (Move saved return_buffer_ptr from R14 to RCX)
        *current_code_ptr++ = REX_WB_PREFIX; // 0x49 (W=1, B=1 for R14)
        *current_code_ptr++ = OPCODE_MOV_RM64_R64; // mov r/m64, r64 (store r14 to rcx)
        *current_code_ptr++ = (MOD_REGISTER << 6) | (MODRM_REG_R14_CODE << 3) | MODRM_REG_RCX; // 0xCE
        gp_reg_idx++; // RCX is now used for the return buffer pointer
    }

    if (sig->num_params > 0 && sig->param_types != NULL) {
        for (int i = 0; i < sig->num_params; ++i) {
            FFI_Type param_type = sig->param_types[i];

            // Load args[i].value_ptr into R10 (temporary register for base address)
            size_t current_arg_value_ptr_offset = (size_t)i * sizeof(FFI_Argument);
            *current_code_ptr++ = REX_WR_PREFIX | REX_B_BIT; // 0x4D (W=1, R=1, B=1)
            *current_code_ptr++ = OPCODE_MOV_R64_RM64; // mov r64, r/m64 (LOAD from memory)
            *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_R10_CODE << 3) | MODRM_REG_R13_CODE); // Mod=01, Reg=R10, R/M=R13_CODE
            *current_code_ptr++ = (unsigned char)current_arg_value_ptr_offset; // disp8

            bool is_current_param_xmm_type = (param_type == FFI_TYPE_FLOAT || param_type == FFI_TYPE_DOUBLE);
            bool is_current_param_int128_type = (param_type == FFI_TYPE_INT128 || param_type == FFI_TYPE_UINT128);
            bool goes_to_reg = false;
            bool to_stack = false;

            if (is_current_param_xmm_type) {
                if (xmm_reg_idx < 4) { // 4 XMM registers for Win64
                    goes_to_reg = true;
                    // MOVSS/MOVSD XMMn, [R10]
                    unsigned char xmm_prefix = (param_type == FFI_TYPE_FLOAT) ? PREFIX_MOVSS : PREFIX_MOVSD;
                    unsigned char xmm_rex_prefix = REX_BASE_0x40_BIT | REX_B_BIT; // REX.B for R10 as base
                    if (xmm_reg_idx >= 4) xmm_rex_prefix |= REX_R_BIT; // For XMM4-XMM7, not applicable here
                    *current_code_ptr++ = xmm_prefix;
                    *current_code_ptr++ = xmm_rex_prefix;
                    *current_code_ptr++ = 0x0F;
                    *current_code_ptr++ = OPCODE_XMM_MOV_XMM_RM; // 0x10
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_XMM0_CODE + xmm_reg_idx << 3) | MODRM_REG_R10_CODE);
                    xmm_reg_idx++;
                } else {
                    to_stack = true;
                }
            } else if (is_current_param_int128_type) {
                // 128-bit integers are passed as two 64-bit arguments on Win64
                if (num_gp_regs_used <= 2) { // Check if there are at least two registers remaining (R9 is gp_reg_idx 3)
                    goes_to_reg = true;
                    // Load lower 64 bits from [R10] into first GPR
                    unsigned char dest_reg_low = gp_arg_regs[gp_reg_idx];
                    bool needs_rex_r_low = gp_arg_regs_needs_rex_r[gp_reg_idx];
                    unsigned char rex_prefix_low = REX_W_PREFIX | REX_B_BIT;
                    if (needs_rex_r_low) rex_prefix_low |= REX_R_BIT;
                    *current_code_ptr++ = rex_prefix_low;
                    *current_code_ptr++ = OPCODE_MOV_R64_RM64; // MOV reg, [mem]
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (dest_reg_low << 3) | MODRM_REG_R10_CODE);

                    // Load upper 64 bits from [R10 + 8] into second GPR
                    unsigned char dest_reg_high = gp_arg_regs[gp_reg_idx + 1];
                    bool needs_rex_r_high = gp_arg_regs_needs_rex_r[gp_reg_idx + 1];
                    unsigned char rex_prefix_high = REX_W_PREFIX | REX_B_BIT;
                    if (needs_rex_r_high) rex_prefix_high |= REX_R_BIT;
                    *current_code_ptr++ = rex_prefix_high;
                    *current_code_ptr++ = OPCODE_MOV_R64_RM64; // MOV reg, [mem]
                    *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (dest_reg_high << 3) | MODRM_REG_R10_CODE);
                    *current_code_ptr++ = 0x08; // disp8 = 8 (for upper 64 bits)

                    gp_reg_idx += 2; // Consumes two GPRs
                } else {
                    to_stack = true;
                }
            } else { // All other types (integers, bool, char, pointer, wchar_t, size_t)
                if (gp_reg_idx < 4) { // 4 GPRs for Win64
                    goes_to_reg = true;
                    unsigned char dest_reg_code = gp_arg_regs[gp_reg_idx];
                    bool use_rex_r_for_dest_reg = gp_arg_regs_needs_rex_r[gp_reg_idx];
                    unsigned char current_opcode;
                    unsigned char modrm = (unsigned char)((MOD_INDIRECT << 6) | (dest_reg_code << 3) | MODRM_REG_R10_CODE);
                    unsigned char final_rex_prefix = REX_BASE_0x40_BIT | REX_B_BIT;
                    if (use_rex_r_for_dest_reg) final_rex_prefix |= REX_R_BIT;

                    switch (param_type) {
                        case FFI_TYPE_BOOL:
                        case FFI_TYPE_CHAR:
                        case FFI_TYPE_UCHAR:
                            current_opcode = 0xB6; // MOVZX r64, r/m8
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_SCHAR:
                            current_opcode = 0xBE; // MOVSX r64, r/m8
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_SHORT:
                        case FFI_TYPE_SSHORT:
                            current_opcode = 0xBF; // MOVSX r64, r/m16
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = 0x66; *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_USHORT:
                            current_opcode = 0xB7; // MOVZX r64, r/m16
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = 0x66; *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_INT:
                        case FFI_TYPE_SINT:
                        case FFI_TYPE_WCHAR: // wchar_t is typically 16-bit on Win64, so it's handled as short
                            // For Win64, wchar_t is 2 bytes. It should be zero-extended to 64-bit.
                            current_opcode = 0xB7; // MOVZX r64, r/m16
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = 0x66; *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_UINT:
                            current_opcode = OPCODE_MOV_R64_RM64; // MOV r32, r/m32 (zero-extends)
                            *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_LONG: // long is 32-bit on Win64
                        case FFI_TYPE_ULONG:
                        case FFI_TYPE_SLONG:
                            current_opcode = 0x63; // MOVSXD r64, r/m32 (for signed)
                            if (param_type == FFI_TYPE_ULONG) { // For unsigned long, it's MOV r32, r/m32
                                current_opcode = OPCODE_MOV_R64_RM64;
                            }
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        case FFI_TYPE_LLONG:
                        case FFI_TYPE_ULLONG:
                        case FFI_TYPE_POINTER:
                        case FFI_TYPE_SIZE_T: // size_t is 64-bit on Win64
                        case FFI_TYPE_SLLONG:
                            current_opcode = OPCODE_MOV_R64_RM64; // MOV r64, r/m64
                            final_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = final_rex_prefix; *current_code_ptr++ = current_opcode; *current_code_ptr++ = modrm;
                            break;
                        default: return 0; // Error
                    }
                    gp_reg_idx++;
                } else {
                    to_stack = true;
                }
            }

            if (to_stack) {
                // Stack arguments are placed at RSP + shadow_space + (stack_arg_current_idx * 8)
                size_t stack_offset_from_rsp_base = 32 + (size_t)stack_arg_current_idx * 8; // 32 for shadow space

                if (is_current_param_int128_type) {
                    // Load lower 64 bits into R11
                    *current_code_ptr++ = REX_WR_PREFIX | REX_B_BIT; // REX.W for 64-bit, REX.R for R11, REX.B for R10
                    *current_code_ptr++ = OPCODE_MOV_R64_RM64; // MOV R11, [R10]
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_R11_CODE << 3) | MODRM_REG_R10_CODE);

                    // Load upper 64 bits into R12 (using R12 as scratch temporarily)
                    *current_code_ptr++ = REX_PUSH_POP_R12_PREFIX | REX_W_PREFIX | REX_R_BIT; // REX.B for R10, REX.R for R12
                    *current_code_ptr++ = OPCODE_MOV_R64_RM64; // MOV R12, [R10 + 8]
                    *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_R12_CODE << 3) | MODRM_REG_R10_CODE);
                    *current_code_ptr++ = 0x08; // disp8 = 8

                    // Store R11 to [RSP + offset_low]
                    size_t stack_offset_low = stack_offset_from_rsp_base;
                    *current_code_ptr++ = REX_W_PREFIX | REX_R_BIT; // REX.W for 64-bit, REX.R for R11
                    *current_code_ptr++ = OPCODE_MOV_RM64_R64; // MOV [RSP + offset_low], R11
                    *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_R11_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_RSP;
                    *current_code_ptr++ = (unsigned char)stack_offset_low;

                    // Store R12 to [RSP + offset_high]
                    size_t stack_offset_high = stack_offset_from_rsp_base + 8;
                    *current_code_ptr++ = REX_W_PREFIX | REX_R_BIT; // REX.W for 64-bit, REX.R for R12
                    *current_code_ptr++ = OPCODE_MOV_RM64_R64; // MOV [RSP + offset_high], R12
                    *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_R12_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_RSP;
                    *current_code_ptr++ = (unsigned char)stack_offset_high;

                    stack_arg_current_idx += 2; // Consumes two stack slots
                } else if (is_current_param_xmm_type) {
                    // Load float/double from (R10) into XMM7 (scratch register)
                    unsigned char xmm_prefix = (param_type == FFI_TYPE_FLOAT) ? PREFIX_MOVSS : PREFIX_MOVSD;
                    *current_code_ptr++ = xmm_prefix;
                    *current_code_ptr++ = (REX_BASE_0x40_BIT | REX_R_BIT | REX_B_BIT); // REX.R for XMM7
                    *current_code_ptr++ = 0x0F;
                    *current_code_ptr++ = OPCODE_XMM_MOV_XMM_RM; // 0x10
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_XMM7_CODE << 3) | MODRM_REG_R10_CODE);

                    // Store float/double from XMM7 to (RSP + stack_offset_from_rsp_base)
                    *current_code_ptr++ = xmm_prefix;
                    *current_code_ptr++ = (REX_BASE_0x40_BIT | REX_R_BIT); // REX.R for XMM7
                    *current_code_ptr++ = 0x0F;
                    *current_code_ptr++ = OPCODE_XMM_MOV_RM_XMM; // 0x11
                    *current_code_ptr++ = (unsigned char)((MOD_DISP8 << 6) | (MODRM_REG_XMM7_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_RSP;
                    *current_code_ptr++ = (unsigned char)stack_offset_from_rsp_base;
                    stack_arg_current_idx++;
                } else { // GPR types to stack
                    // Load the value from (R10) into R11 (temporary register)
                    unsigned char load_rex_prefix = REX_BASE_0x40_BIT | REX_B_BIT | REX_R_BIT; // R10 is base, R11 is dest (R11's code is 0x03, needs REX.R)
                    unsigned char load_opcode;
                    unsigned char load_modrm = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_R11_CODE << 3) | MODRM_REG_R10_CODE);

                    switch (param_type) {
                        case FFI_TYPE_BOOL: case FFI_TYPE_CHAR: case FFI_TYPE_UCHAR:
                            load_opcode = 0xB6; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_SCHAR:
                            load_opcode = 0xBE; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_SHORT: case FFI_TYPE_SSHORT:
                            load_opcode = 0xBF; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = 0x66; *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_USHORT:
                            load_opcode = 0xB7; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = 0x66; *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_INT: case FFI_TYPE_SINT:
                        case FFI_TYPE_LONG: case FFI_TYPE_ULONG: case FFI_TYPE_SLONG: // 32-bit on Win64
                            load_opcode = 0x63; load_rex_prefix |= REX_W_PREFIX;
                            if (param_type == FFI_TYPE_ULONG) { // For unsigned long, it's MOV r32, r/m32
                                load_opcode = OPCODE_MOV_R64_RM64;
                                load_rex_prefix &= ~REX_W_PREFIX; // Clear W bit for 32-bit move
                            }
                            *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_WCHAR: // wchar_t is 16-bit on Win64
                            load_opcode = 0xB7; load_rex_prefix |= REX_W_PREFIX; // MOVZX r64, r/m16
                            *current_code_ptr++ = 0x66; *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = 0x0F; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        case FFI_TYPE_LLONG: case FFI_TYPE_ULLONG:
                        case FFI_TYPE_POINTER: case FFI_TYPE_SIZE_T: case FFI_TYPE_SLLONG:
                            load_opcode = OPCODE_MOV_R64_RM64; load_rex_prefix |= REX_W_PREFIX;
                            *current_code_ptr++ = load_rex_prefix; *current_code_ptr++ = load_opcode; *current_code_ptr++ = load_modrm;
                            break;
                        default: return 0; // Error
                    }

                    // Step 2: Store the value from R11 to (RSP + stack_offset_from_rsp_base)
                    unsigned char store_rex_prefix = REX_W_PREFIX | REX_R_BIT;
                    *current_code_ptr++ = store_rex_prefix;
                    *current_code_ptr++ = OPCODE_MOV_RM64_R64;
                    unsigned char store_modrm_mod = (stack_offset_from_rsp_base == 0) ? MOD_INDIRECT : MOD_DISP8;
                    *current_code_ptr++ = (unsigned char)((store_modrm_mod << 6) | (MODRM_REG_R11_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_RSP;
                    if (stack_offset_from_rsp_base != 0) {
                        *current_code_ptr++ = (unsigned char)stack_offset_from_rsp_base;
                    }
                    stack_arg_current_idx++;
                }
            }
        }
    }

    // --- Call Target Function ---
    // movabs RAX, <target_func_address>
    *current_code_ptr++ = REX_W_PREFIX; // REX.W prefix for 64-bit operation
    *current_code_ptr++ = OPCODE_MOV_IMM64_RAX;

    target_addr_val = (long)(uintptr_t)sig->func_ptr; // Get the 64-bit address as a long
    memcpy(current_code_ptr, &target_addr_val, 8);
    current_code_ptr += 8;

    // call RAX
    *current_code_ptr++ = OPCODE_CALL_RM64; // CALL r/m64
    *current_code_ptr++ = (unsigned char)((MOD_REGISTER << 6) | (0x02 << 3) | MODRM_REG_RAX);

    // --- Return Value Handling ---
    // Store return value (from EAX/RAX or XMM0) into (R14)
    if (sig->return_type != FFI_TYPE_VOID) {
        if (return_struct_by_pointer) {
            // If returning by pointer, the target function returns the pointer in RAX.
            // We don't need to do anything here as the target function already wrote to [R14].
            // We just need to ensure the original return_buffer_ptr is in R14.
            // (The target function wrote to RCX, which was our R14).
            // So, no explicit mov here.
        } else {
            switch (sig->return_type) {
                case FFI_TYPE_BOOL:
                case FFI_TYPE_CHAR:
                case FFI_TYPE_UCHAR:
                case FFI_TYPE_SCHAR:
                    // movb [R14], AL
                    *current_code_ptr++ = REX_B_PREFIX_32BIT_OP; // 0x41 (for R14 as base)
                    *current_code_ptr++ = 0x88; // MOV r/m8, r8 (AL is 8-bit part of RAX)
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_RAX << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_R14_BASE;
                    break;
                case FFI_TYPE_SHORT:
                case FFI_TYPE_USHORT:
                case FFI_TYPE_SSHORT:
                case FFI_TYPE_WCHAR: // wchar_t is 16-bit on Win64
                    // movw [R14], AX
                    *current_code_ptr++ = 0x66; // Operand-size override prefix for 16-bit
                    *current_code_ptr++ = REX_B_PREFIX_32BIT_OP; // 0x41 (for R14 as base)
                    *current_code_ptr++ = 0x89; // MOV r/m16, r16 (AX is 16-bit part of RAX)
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_RAX << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_R14_BASE;
                    break;
                case FFI_TYPE_INT:
                case FFI_TYPE_UINT:
                case FFI_TYPE_SINT:
                case FFI_TYPE_LONG: // 32-bit on Win64
                case FFI_TYPE_ULONG:
                case FFI_TYPE_SLONG:
                    // movl [R14], EAX
                    *current_code_ptr++ = REX_B_PREFIX_32BIT_OP; // 0x41
                    *current_code_ptr++ = OPCODE_MOV_RM64_R64;   // 0x89
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_RAX << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_R14_BASE;
                    break;
                case FFI_TYPE_LLONG:
                case FFI_TYPE_ULLONG:
                case FFI_TYPE_POINTER:
                case FFI_TYPE_SIZE_T:
                case FFI_TYPE_SLLONG:
                    // movq [R14], RAX
                    *current_code_ptr++ = REX_WB_PREFIX;         // 0x49
                    *current_code_ptr++ = OPCODE_MOV_RM64_R64;   // 0x89
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_RAX << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_R14_BASE;
                    break;
                case FFI_TYPE_FLOAT:
                    // movss [R14], XMM0
                    *current_code_ptr++ = PREFIX_MOVSS;          // 0xF3
                    *current_code_ptr++ = (REX_BASE_0x40_BIT | REX_B_BIT); // 0x41
                    *current_code_ptr++ = 0x0F;
                    *current_code_ptr++ = OPCODE_XMM_MOV_RM_XMM; // 0x11
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_XMM0_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_R14_BASE;
                    break;
                case FFI_TYPE_DOUBLE:
                    // movsd [R14], XMM0
                    *current_code_ptr++ = PREFIX_MOVSD;          // 0xF2
                    *current_code_ptr++ = (REX_BASE_0x40_BIT | REX_B_BIT); // 0x41
                    *current_code_ptr++ = 0x0F;
                    *current_code_ptr++ = OPCODE_XMM_MOV_RM_XMM; // 0x11
                    *current_code_ptr++ = (unsigned char)((MOD_INDIRECT << 6) | (MODRM_REG_XMM0_CODE << 3) | RM_SIB_BYTE_FOLLOWS);
                    *current_code_ptr++ = SIB_BYTE_R14_BASE;
                    break;
                default: // This default case catches FFI_TYPE_UNKNOWN or any other unsupported type for return
                    return 0; // Indicate error
            }
        }
    }

    // --- Epilogue ---
    // Reverse stack alignment
    if (total_stack_alloc > 0) {
        *current_code_ptr++ = REX_W_PREFIX; // REX.W prefix for 64-bit operation
        *current_code_ptr++ = OPCODE_ADD_IMM8_RSP; // 0x83 (ADD r/m64, imm8)
        *current_code_ptr++ = (MOD_REGISTER << 6) | (0x00 << 3) | MODRM_REG_RSP; // Mod=11, Reg=Group 0 (ADD), R/M=RSP (0x04) -> 0xC4
        *current_code_ptr++ = (unsigned char)total_stack_alloc; // imm8
    }

    // Pop R14 to restore its original value
    *current_code_ptr++ = REX_PUSH_POP_R14_PREFIX; // 0x41
    *current_code_ptr++ = OPCODE_POP_R14_BYTE;     // 0x5E

    // Pop R13 to restore its original value
    *current_code_ptr++ = REX_PUSH_POP_R13_PREFIX; // 0x41
    *current_code_ptr++ = OPCODE_POP_R13_BYTE;     // 0x5D

    // pop RBP
    *current_code_ptr++ = OPCODE_POP_RBP;

    // ret
    *current_code_ptr++ = OPCODE_RET;

    return (size_t)(current_code_ptr - code_buffer);
}
#endif // FFI_ARCH_X64

#ifdef FFI_ARCH_ARM64
/**
 * @brief Generates ARM64 AAPCS trampoline bytes.
 * This is a highly simplified and conceptual implementation.
 * A complete ARM64 trampoline would require precise instruction encoding.
 * @param code_buffer Pointer to the memory where the assembly bytes will be written.
 * @param sig A pointer to the FFI_FunctionSignature.
 * @return The size of the generated assembly code in bytes.
 */
size_t generate_arm64_aapcs_trampoline(unsigned char* code_buffer, FFI_FunctionSignature* sig) {
    uint32_t *current_code_ptr = (uint32_t*)code_buffer;
    uint64_t target_addr_val = (uint64_t)sig->func_ptr;

    // The trampoline itself will be called by C with this signature:
    // void (*GenericTrampoline)(FFI_Argument* args, int num_args, void* return_buffer_ptr)
    // Register mapping for this trampoline call (AAPCS64):
    // X0: FFI_Argument* args (base address of the FFI_Argument array)
    // X1: int num_args (number of arguments in the array)
    // X2: void* return_buffer_ptr (pointer to where the return value should be stored)

    // --- Prologue ---
    // Save X29 (FP) and X30 (LR) to stack, adjust SP.
    // STP X29, X30, [SP, #-16]!
    *current_code_ptr++ = 0xA9BF7BFD; // STP X29, X30, [SP, #-16]!

    // MOV X29, SP (Frame Pointer = Stack Pointer)
    *current_code_ptr++ = 0xF90003BD; // MOV X29, SP (actually ADD X29, SP, #0)

    // Save callee-saved registers: X19-X30, V8-V15
    // We'll use X19 (args_ptr) and X20 (return_buffer_ptr) as callee-saved.
    // X0, X1, X2 are caller-saved and will be overwritten.

    // STP X19, X20, [SP, #-16]!
    *current_code_ptr++ = 0xA9BF4FF3; // STP X19, X20, [SP, #-16]!

    // MOV X19, X0 (Save args pointer from X0 to X19)
    *current_code_ptr++ = 0xAA0003F3; // MOV X19, X0

    // MOV X20, X2 (Save return_buffer_ptr from X2 to X20)
    *current_code_ptr++ = 0xAA0203F4; // MOV X20, X2

    // --- Determine Stack Arguments and Calculate Total Stack Space ---
    int num_gp_regs_used = 0;
    int num_xmm_regs_used = 0;
    int num_stack_args = 0; // Number of 8-byte slots needed on stack

    // AAPCS64 GPRs: X0-X7 (8 registers)
    // AAPCS64 FP/SIMD: V0-V7 (8 registers)

    // If returning a struct > 16 bytes, the return buffer pointer is the first argument (X0).
    bool return_struct_by_pointer = false;
    if (sig->return_type == FFI_TYPE_INT128 || sig->return_type == FFI_TYPE_UINT128) {
        // AAPCS64 returns 128-bit integers in X0/X1. No pointer needed.
        // If it were a struct > 16 bytes, X0 would be used for the return buffer pointer.
        // For now, assume 128-bit fits in X0/X1.
    } else if (sig->return_type == FFI_TYPE_UNKNOWN) {
        // Placeholder for larger structs
    }

    for (int i = 0; i < sig->num_params; ++i) {
        FFI_Type param_type = sig->param_types[i];
        if (param_type == FFI_TYPE_FLOAT || param_type == FFI_TYPE_DOUBLE) {
            if (num_xmm_regs_used < 8) { // AAPCS64 has 8 V registers
                num_xmm_regs_used++;
            } else {
                num_stack_args++;
            }
        } else if (param_type == FFI_TYPE_INT128 || param_type == FFI_TYPE_UINT128) {
            // 128-bit integers consume two GPRs or 16 bytes on stack
            if (num_gp_regs_used <= 6) { // If num_gp_regs_used is 7, we only have X7 left, so it spills.
                num_gp_regs_used += 2; // Consumes two GPRs
            } else {
                num_stack_args += 2; // Consumes 16 bytes on stack
            }
        } else { // All other types go to GPRs
            if (num_gp_regs_used < 8) { // AAPCS64 has 8 GPRs
                num_gp_regs_used++;
            } else {
                num_stack_args++;
            }
        }
    }

    size_t stack_args_total_size = (size_t)num_stack_args * 8;
    size_t final_stack_subtraction = stack_args_total_size;

    // SP must be 16-byte aligned.
    // After STP X29, X30, [SP, #-16]! and STP X19, X20, [SP, #-16]!, SP is 32 bytes lower.
    // So SP is already 16-byte aligned.
    // We just need to ensure the total stack allocation for arguments is 16-byte aligned.
    if ((final_stack_subtraction % 16) != 0) {
        final_stack_subtraction += 8; // Pad to 16-byte alignment
    }

    if (final_stack_subtraction > 0) {
        // SUB SP, SP, #final_stack_subtraction
        *current_code_ptr++ = 0xD1000000 | ((final_stack_subtraction & 0xFFF) << 10) | (ARM64_REG_SP << 5) | ARM64_REG_SP;
    }

    // --- Argument Marshalling ---
    int gp_reg_idx = 0;
    int xmm_reg_idx = 0;
    int stack_arg_current_idx = 0; // 0-indexed counter for arguments going to stack

    // Array of GPR argument registers in order for AAPCS64: X0-X7
    unsigned char gp_arg_regs[] = { ARM64_REG_X0, ARM64_REG_X1, ARM64_REG_X2, ARM64_REG_X3, ARM64_REG_X4, ARM64_REG_X5, ARM64_REG_X6, ARM64_REG_X7 };
    // Array of FP argument registers in order for AAPCS64: V0-V7
    unsigned char fp_arg_regs[] = { ARM64_REG_V0, ARM64_REG_V1, ARM64_REG_V2, ARM64_REG_V3, ARM64_REG_V4, ARM64_REG_V5, ARM64_REG_V6, ARM64_REG_V7 };

    // If returning a struct by pointer, the return buffer pointer is the first argument (X0)
    // and subsequent arguments shift.
    if (return_struct_by_pointer) {
        // MOV X0, X20 (Move saved return_buffer_ptr from X20 to X0)
        *current_code_ptr++ = 0xAA1403E0; // MOV X0, X20
        gp_reg_idx++; // X0 is now used for the return buffer pointer
    }

    if (sig->num_params > 0 && sig->param_types != NULL) {
        for (int i = 0; i < sig->num_params; ++i) {
            FFI_Type param_type = sig->param_types[i];

            // Load args[i].value_ptr into X8 (temporary register for base address)
            size_t current_arg_value_ptr_offset = (size_t)i * sizeof(FFI_Argument);
            // LDR X8, [X19, #offset] (X19 holds the base address of FFI_Argument array)
            *current_code_ptr++ = ARM64_LDR_X_X_IMM(ARM64_REG_X8, ARM64_REG_X19, current_arg_value_ptr_offset);

            bool is_current_param_xmm_type = (param_type == FFI_TYPE_FLOAT || param_type == FFI_TYPE_DOUBLE);
            bool is_current_param_int128_type = (param_type == FFI_TYPE_INT128 || param_type == FFI_TYPE_UINT128);
            bool goes_to_reg = false;
            bool to_stack = false;

            if (is_current_param_xmm_type) {
                if (xmm_reg_idx < 8) { // 8 V registers for AAPCS64
                    goes_to_reg = true;
                    // LDR Sn, [X8] (for float) or LDR Dn, [X8] (for double)
                    if (param_type == FFI_TYPE_FLOAT) {
                        *current_code_ptr++ = 0xBC400100 | (fp_arg_regs[xmm_reg_idx] << 0) | (ARM64_REG_X8 << 5); // LDR Sn, [X8]
                    } else { // FFI_TYPE_DOUBLE
                        *current_code_ptr++ = 0xBC800100 | (fp_arg_regs[xmm_reg_idx] << 0) | (ARM64_REG_X8 << 5); // LDR Dn, [X8]
                    }
                    xmm_reg_idx++;
                } else {
                    to_stack = true;
                }
            } else if (is_current_param_int128_type) {
                // 128-bit integers consume two GPRs
                if (gp_reg_idx <= 6) { // Check if at least two GPRs are available (X0-X7)
                    goes_to_reg = true;
                    // LDR Xn, [X8] (lower 64 bits)
                    *current_code_ptr++ = ARM64_LDR_X_X_IMM(gp_arg_regs[gp_reg_idx], ARM64_REG_X8, 0);
                    // LDR Xn+1, [X8, #8] (upper 64 bits)
                    *current_code_ptr++ = ARM64_LDR_X_X_IMM(gp_arg_regs[gp_reg_idx+1], ARM64_REG_X8, 8);
                    gp_reg_idx += 2; // Consume two GPRs
                } else {
                    to_stack = true;
                }
            } else { // All other types (integers, bool, char, pointer, wchar_t, size_t)
                if (gp_reg_idx < 8) { // 8 GPRs for AAPCS64
                    goes_to_reg = true;
                    unsigned char dest_reg = gp_arg_regs[gp_reg_idx];
                    // Load value from [X8] into dest_reg, with appropriate zero/sign extension
                    switch (param_type) {
                        case FFI_TYPE_BOOL:
                        case FFI_TYPE_CHAR:
                        case FFI_TYPE_UCHAR:
                            // LDRB Wd, [Xn] (zero-extends to 32-bit, then to 64-bit)
                            *current_code_ptr++ = 0x38400100 | (dest_reg << 0) | (ARM64_REG_X8 << 5);
                            break;
                        case FFI_TYPE_SCHAR:
                            // LDRSB Wd, [Xn] (sign-extends to 32-bit, then to 64-bit)
                            *current_code_ptr++ = 0x38C00100 | (dest_reg << 0) | (ARM64_REG_X8 << 5);
                            break;
                        case FFI_TYPE_SHORT:
                        case FFI_TYPE_USHORT:
                        case FFI_TYPE_WCHAR: // wchar_t is 4 bytes on Linux/macOS, 2 bytes on Win64. Assume 4 for ARM64.
                            // LDRH Wd, [Xn] (zero-extends for unsigned, sign-extends for signed)
                            if (param_type == FFI_TYPE_USHORT || param_type == FFI_TYPE_WCHAR) {
                                *current_code_ptr++ = 0x78400100 | (dest_reg << 0) | (ARM64_REG_X8 << 5);
                            } else { // FFI_TYPE_SHORT, FFI_TYPE_SSHORT
                                *current_code_ptr++ = 0x78C00100 | (dest_reg << 0) | (ARM64_REG_X8 << 5);
                            }
                            break;
                        case FFI_TYPE_SSHORT:
                            // LDRSH Wd, [Xn] (sign-extends to 32-bit, then to 64-bit)
                            *current_code_ptr++ = 0x78C00100 | (dest_reg << 0) | (ARM64_REG_X8 << 5);
                            break;
                        case FFI_TYPE_INT:
                        case FFI_TYPE_UINT:
                        case FFI_TYPE_SINT:
                        case FFI_TYPE_LONG: // 64-bit on AAPCS64
                        case FFI_TYPE_ULONG:
                        case FFI_TYPE_LLONG:
                        case FFI_TYPE_ULLONG:
                        case FFI_TYPE_POINTER:
                        case FFI_TYPE_SIZE_T:
                        case FFI_TYPE_SLONG:
                        case FFI_TYPE_SLLONG:
                            // LDR Xd, [Xn] (64-bit load)
                            *current_code_ptr++ = ARM64_LDR_X_X_IMM(dest_reg, ARM64_REG_X8, 0);
                            break;
                        default: return 0; // Error
                    }
                    gp_reg_idx++;
                } else {
                    to_stack = true;
                }
            }

            if (to_stack) {
                // Stack arguments are placed relative to SP.
                // Current SP is already adjusted by prologue and for alignment.
                // The stack arguments start at SP + 0, then SP + 8, etc.
                size_t stack_offset_from_sp = (size_t)stack_arg_current_idx * 8;

                if (is_current_param_int128_type) {
                    // LDR X9, [X8] (lower 64 bits into scratch X9)
                    *current_code_ptr++ = ARM64_LDR_X_X_IMM(ARM64_REG_X9, ARM64_REG_X8, 0);
                    // LDR X10, [X8, #8] (upper 64 bits into scratch X10)
                    *current_code_ptr++ = ARM64_LDR_X_X_IMM(ARM64_REG_X10, ARM64_REG_X8, 8);

                    // STR X9, [SP, #offset_low]
                    *current_code_ptr++ = ARM64_STR_X_X_IMM(ARM64_REG_X9, ARM64_REG_SP, stack_offset_from_sp);
                    // STR X10, [SP, #offset_high]
                    *current_code_ptr++ = ARM64_STR_X_X_IMM(ARM64_REG_X10, ARM64_REG_SP, stack_offset_from_sp + 8);
                    stack_arg_current_idx += 2;
                } else if (is_current_param_xmm_type) {
                    // LDR Dn, [X8] (load into scratch V8)
                    if (param_type == FFI_TYPE_FLOAT) {
                        *current_code_ptr++ = 0xBC400100 | (ARM64_REG_V8 << 0) | (ARM64_REG_X8 << 5); // LDR S8, [X8]
                    } else { // FFI_TYPE_DOUBLE
                        *current_code_ptr++ = 0xBC800100 | (ARM64_REG_V8 << 0) | (ARM64_REG_X8 << 5); // LDR D8, [X8]
                    }
                    // STR Dn, [SP, #offset]
                    if (param_type == FFI_TYPE_FLOAT) {
                        *current_code_ptr++ = 0xBC000100 | (ARM64_REG_V8 << 0) | (ARM64_REG_SP << 5) | ((stack_offset_from_sp/4) << 10); // STR S8, [SP, #offset]
                    } else { // FFI_TYPE_DOUBLE
                        *current_code_ptr++ = 0xBC200100 | (ARM64_REG_V8 << 0) | (ARM64_REG_SP << 5) | ((stack_offset_from_sp/8) << 10); // STR D8, [SP, #offset]
                    }
                    stack_arg_current_idx++;
                } else { // GPR types to stack
                    // LDR X9, [X8] (load into scratch X9)
                    *current_code_ptr++ = ARM64_LDR_X_X_IMM(ARM64_REG_X9, ARM64_REG_X8, 0);
                    // STR X9, [SP, #offset]
                    *current_code_ptr++ = ARM64_STR_X_X_IMM(ARM64_REG_X9, ARM64_REG_SP, stack_offset_from_sp);
                    stack_arg_current_idx++;
                }
            }
        }
    }

    // --- Call Target Function ---
    // LDR X16, #target_addr_val_offset (load target address into X16 - temporary)
    // This requires a PC-relative load or a literal pool. For simplicity, we'll use MOV.
    // MOV X16, #high_part
    // MOVK X16, #low_part
    // This is a simplified direct load. A real implementation would use ADRP/ADD or LDR literal.
    // For now, assuming direct 64-bit immediate load (which is not directly possible in one instruction)
    // This needs to be done in multiple instructions: MOVZ, MOVK.
    // Example: MOVZ X16, #lower16
    // MOVK X16, #mid16, LSL #16
    // MOVK X16, #mid_high16, LSL #32
    // MOVK X16, #upper16, LSL #48

    // This is a placeholder for a 64-bit address load.
    // A proper implementation would use ADRP/ADD or LDR literal.
    // For now, we'll just put the address in X16 (assuming it's a small offset or direct load is possible for testing)
    // This is the most complex part of ARM64 assembly generation.
    // For simplicity, we'll assume the target address is loaded into X16.
    // A more robust solution would involve a literal pool or ADRP/ADD.
    // For now, we'll just use a placeholder for the target address.
    // The `BLR X16` instruction will branch to the address in X16.

    // Load target_addr_val into X16 (scratch register)
    // This is a simplified sequence for loading a 64-bit constant.
    // It assumes the address fits in a sequence of MOVZ/MOVK instructions.
    // For a full 64-bit arbitrary address, it takes 4 instructions.
    // MOVZ X16, #lower_16_bits
    *current_code_ptr++ = 0xD2800000 | ((target_addr_val & 0xFFFF) << 5) | (ARM64_REG_X16);
    // MOVK X16, #next_16_bits, LSL #16
    *current_code_ptr++ = 0xF2800000 | (((target_addr_val >> 16) & 0xFFFF) << 5) | (1 << 21) | (ARM64_REG_X16);
    // MOVK X16, #next_16_bits, LSL #32
    *current_code_ptr++ = 0xF2800000 | (((target_addr_val >> 32) & 0xFFFF) << 5) | (2 << 21) | (ARM64_REG_X16);
    // MOVK X16, #upper_16_bits, LSL #48
    *current_code_ptr++ = 0xF2800000 | (((target_addr_val >> 48) & 0xFFFF) << 5) | (3 << 21) | (ARM64_REG_X16);


    // BLR X16 (Branch with Link to Register X16)
    *current_code_ptr++ = ARM64_BLR(ARM64_REG_X16);

    // --- Return Value Handling ---
    // Store return value (from X0/V0) into (X20)
    if (sig->return_type != FFI_TYPE_VOID) {
        if (return_struct_by_pointer) {
            // If returning by pointer, the target function wrote to X0 (our X20).
            // No explicit store needed here.
        } else {
            switch (sig->return_type) {
                case FFI_TYPE_BOOL:
                case FFI_TYPE_CHAR:
                case FFI_TYPE_UCHAR:
                case FFI_TYPE_SCHAR:
                    // STRB W0, [X20]
                    *current_code_ptr++ = 0x38000000 | (ARM64_REG_X0 << 0) | (ARM64_REG_X20 << 5);
                    break;
                case FFI_TYPE_SHORT:
                case FFI_TYPE_USHORT:
                case FFI_TYPE_SSHORT:
                case FFI_TYPE_WCHAR:
                    // STRH W0, [X20]
                    *current_code_ptr++ = 0x78000000 | (ARM64_REG_X0 << 0) | (ARM64_REG_X20 << 5);
                    break;
                case FFI_TYPE_INT:
                case FFI_TYPE_UINT:
                case FFI_TYPE_SINT:
                case FFI_TYPE_LONG:
                case FFI_TYPE_ULONG:
                case FFI_TYPE_SLONG:
                    // STR W0, [X20] (store 32-bit value)
                    *current_code_ptr++ = 0xB8000000 | (ARM64_REG_X0 << 0) | (ARM64_REG_X20 << 5);
                    break;
                case FFI_TYPE_LLONG:
                case FFI_TYPE_ULLONG:
                case FFI_TYPE_POINTER:
                case FFI_TYPE_SIZE_T:
                case FFI_TYPE_SLLONG:
                    // STR X0, [X20] (store 64-bit value)
                    *current_code_ptr++ = ARM64_STR_X_X_IMM(ARM64_REG_X0, ARM64_REG_X20, 0);
                    break;
                case FFI_TYPE_FLOAT:
                    // STR S0, [X20]
                    *current_code_ptr++ = 0xBC000000 | (ARM64_REG_V0 << 0) | (ARM64_REG_X20 << 5);
                    break;
                case FFI_TYPE_DOUBLE:
                    // STR D0, [X20]
                    *current_code_ptr++ = 0xBC200000 | (ARM64_REG_V0 << 0) | (ARM64_REG_X20 << 5);
                    break;
                case FFI_TYPE_INT128:
                case FFI_TYPE_UINT128:
                    // Return value in X0:X1 (X0 is lower 64, X1 is upper 64)
                    // STR X0, [X20] (store lower 64)
                    *current_code_ptr++ = ARM64_STR_X_X_IMM(ARM64_REG_X0, ARM64_REG_X20, 0);
                    // STR X1, [X20, #8] (store upper 64)
                    *current_code_ptr++ = ARM64_STR_X_X_IMM(ARM64_REG_X1, ARM64_REG_X20, 8);
                    break;
                default: return 0; // Indicate error
            }
        }
    }

    // --- Epilogue ---
    // Reverse stack alignment
    if (final_stack_subtraction > 0) {
        // ADD SP, SP, #final_stack_subtraction
        *current_code_ptr++ = 0x91000000 | ((final_stack_subtraction & 0xFFF) << 10) | (ARM64_REG_SP << 5) | ARM64_REG_SP;
    }

    // Restore callee-saved registers
    // LDP X19, X20, [SP], #16
    *current_code_ptr++ = 0xA8C14FF3; // LDP X19, X20, [SP], #16

    // LDP X29, X30, [SP], #16
    *current_code_ptr++ = 0xA8C17BFD; // LDP X29, X30, [SP], #16

    // RET (return from function)
    *current_code_ptr++ = ARM64_RET();

    return (size_t)((unsigned char*)current_code_ptr - code_buffer);
}
#endif // FFI_ARCH_ARM64


/**
 * @brief Generates assembly bytes for a generic trampoline based on a function signature.
 * It dynamically marshals arguments into registers and handles return values.
 *
 * @param code_buffer Pointer to the memory where the assembly bytes will be written.
 * @param sig A pointer to the FFI_FunctionSignature containing all metadata for the target function.
 * @return The size of the generated assembly code in bytes.
 */
size_t generate_generic_trampoline(unsigned char* code_buffer, FFI_FunctionSignature* sig) {
#ifdef FFI_ARCH_X64
    #ifdef FFI_OS_LINUX
        diag("Generating x86-64 System V trampoline for '%s'.", sig->debug_name);
        return generate_x86_64_sysv_trampoline(code_buffer, sig);
    #elif defined(FFI_OS_WIN64)
        diag("Generating x86-64 Win64 trampoline for '%s'.", sig->debug_name);
        return generate_x86_64_win64_trampoline(code_buffer, sig);
    #elif defined(FFI_OS_MACOS)
        diag("Generating x86-64 System V (macOS) trampoline for '%s'.", sig->debug_name);
        return generate_x86_64_sysv_trampoline(code_buffer, sig); // macOS uses System V
    #else
        BAIL_OUT("Unsupported x86-64 OS for trampoline generation.");
        return 0;
    #endif
#elif defined(FFI_ARCH_ARM64)
    diag("Generating ARM64 AAPCS trampoline for '%s'.", sig->debug_name);
    return generate_arm64_aapcs_trampoline(code_buffer, sig);
#else
    BAIL_OUT("Unsupported architecture for trampoline generation.");
    return 0;
#endif
}

/**
 * @brief Creates and initializes an FFI_FunctionSignature object.
 * Allocates memory for the struct and its trampoline code, and generates the assembly.
 *
 * @param debug_name A string name for debugging purposes.
 * @param return_type The return type of the C function.
 * @param num_params The number of parameters the C function expects.
 * @param param_types An array of FFI_Type representing the parameter types, or NULL if num_params is 0.
 * @param func_ptr A pointer to the actual C function implementation.
 * @param manual_trampoline_bytes Optional. Pointer to a byte array for a pre-defined trampoline.
 * @param manual_trampoline_size Optional. Size of the pre-defined trampoline byte array.
 * @return A pointer to the newly created FFI_FunctionSignature object, or NULL on failure.
 */
FFI_FunctionSignature* create_ffi_function(const char* debug_name, FFI_Type return_type,
                                            int num_params, FFI_Type* param_types,
                                            GenericFuncPtr func_ptr,
                                            unsigned char* manual_trampoline_bytes,
                                            size_t manual_trampoline_size) {
    diag("create_ffi_function: Entering for '%s'. func_ptr received: %p", debug_name, (void*)func_ptr);
    FFI_FunctionSignature* new_ffi_func = (FFI_FunctionSignature*)malloc(sizeof(FFI_FunctionSignature));
    if (new_ffi_func == NULL) {
        diag("Failed to allocate FFI_FunctionSignature for '%s'", debug_name);
        return NULL;
    }
    new_ffi_func->debug_name = debug_name;
    new_ffi_func->return_type = return_type;
    new_ffi_func->num_params = num_params;
    new_ffi_func->param_types = param_types; // Point to static array or dynamically copy if needed
    new_ffi_func->func_ptr = func_ptr;
    new_ffi_func->trampoline_size = 512; // Increased size to 512 bytes for more complex trampolines
    // Cast to void* before assigning to function pointer type to avoid ISO C warning
    new_ffi_func->trampoline_code = (GenericTrampolinePtr)(void*)ffi_create_executable_memory(new_ffi_func->trampoline_size);
    if (new_ffi_func->trampoline_code == NULL) {
        free(new_ffi_func);
        return NULL;
    }

    size_t actual_code_size;
    if (manual_trampoline_bytes && manual_trampoline_size > 0) {
        diag("Using manual trampoline bytes for '%s'. Size: %zu", debug_name, manual_trampoline_size);
        if (manual_trampoline_size > new_ffi_func->trampoline_size) {
            diag("ERROR: Manual trampoline size (%zu) exceeds allocated memory (%zu).",
                    manual_trampoline_size, new_ffi_func->trampoline_size);
            // Cast to void* for ffi_free_executable_memory
            ffi_free_executable_memory((void*)new_ffi_func->trampoline_code, new_ffi_func->trampoline_size);
            free(new_ffi_func);
            return NULL;
        }
        // Cast to void* for memcpy destination
        memcpy((void*)new_ffi_func->trampoline_code, manual_trampoline_bytes, manual_trampoline_size);
        actual_code_size = manual_trampoline_size;
    } else {
        // Cast to unsigned char* for generate_generic_trampoline
        actual_code_size = generate_generic_trampoline((unsigned char*)new_ffi_func->trampoline_code, new_ffi_func);
        if (actual_code_size == 0 || actual_code_size > new_ffi_func->trampoline_size) {
            diag("ERROR: Trampoline generation issue for '%s': size %zu, allocated %zu. Cleaning up.",
                    debug_name, actual_code_size, new_ffi_func->trampoline_size);
            // Cast to void* for ffi_free_executable_memory
            ffi_free_executable_memory((void*)new_ffi_func->trampoline_code, new_ffi_func->trampoline_size);
            free(new_ffi_func);
            return NULL;
        }
    }

    // Flush instruction cache after writing executable code
    ffi_flush_instruction_cache((void*)new_ffi_func->trampoline_code, actual_code_size);
    // Cast to void* for printf %p
    diag("Generated trampoline for '%s' at %p (size: %zu bytes). Target func: %p",
           new_ffi_func->debug_name, (void*)new_ffi_func->trampoline_code, actual_code_size, (void*)new_ffi_func->func_ptr);
    // Print raw bytes for debugging - MODIFIED TO PRINT HEX DUMP
    diag("Raw trampoline bytes (hex) for '%s':", new_ffi_func->debug_name); // Corrected line
    char line_buffer[128]; // Buffer for one line of hex dump
    int offset_in_line = 0;
    int chars_written;

    // Determine instruction size for hex dump formatting
    size_t instruction_size = 
#ifdef FFI_ARCH_ARM64
    4; // ARM64 instructions are 4 bytes
#else 
	1; // Default for x86-64
#endif

    for (size_t i = 0; i < actual_code_size; i += instruction_size) {
        if (i % (16 / instruction_size * instruction_size) == 0) { // Start of a new line (16 bytes per line)
            if (i > 0) {
                diag("%s", line_buffer); // Print the previous line
            }
            // Start new line with address
            chars_written = snprintf(line_buffer + offset_in_line, sizeof(line_buffer), "%p: ", (void*)((uintptr_t)new_ffi_func->trampoline_code + i));
            offset_in_line = chars_written;
        }
        // Append hex byte(s)
        for (size_t j = 0; j < instruction_size; ++j) {
            chars_written = snprintf(line_buffer + offset_in_line, sizeof(line_buffer) - offset_in_line, "%02x", ((unsigned char*)(void*)new_ffi_func->trampoline_code)[i + j]);
            offset_in_line += chars_written;
        }
        chars_written = snprintf(line_buffer + offset_in_line, sizeof(line_buffer) - offset_in_line, " ");
        offset_in_line += chars_written;

        if ((i + instruction_size) % (8 / instruction_size * instruction_size) == 0 && (i + instruction_size) % (16 / instruction_size * instruction_size) != 0) { // Add extra space after 8 bytes (but not at 16)
            chars_written = snprintf(line_buffer + offset_in_line, sizeof(line_buffer) - offset_in_line, " ");
            offset_in_line += chars_written;
        }
    }
    if (offset_in_line > 0) { // Print any remaining bytes on the last line
        diag("%s", line_buffer);
    }
    diag(""); // Ensure a newline at the very end

    return new_ffi_func;
}

/**
 * @brief Destroys an FFI_FunctionSignature object, freeing its associated memory.
 *
 * @param ffi_func A pointer to the FFI_FunctionSignature object to destroy.
 */
void destroy_ffi_function(FFI_FunctionSignature* ffi_func) {
    if (ffi_func) {
        diag("Destroying FFI function: '%s'", ffi_func->debug_name);
        if (ffi_func->trampoline_code) {
            // Cast to void* for ffi_free_executable_memory
            ffi_free_executable_memory((void*)ffi_func->trampoline_code, ffi_func->trampoline_size);
            ffi_func->trampoline_code = NULL;
        }
        free(ffi_func);
    }
}

/**
 * @brief Invokes a foreign C function using its dynamically generated trampoline.
 * This function acts as the "core VM dispatcher".
 *
 * @param sig A pointer to the FFI_FunctionSignature object representing the target function.
 * @param args An array of FFI_Argument structs holding the input arguments. Can be NULL if num_args is 0.
 * @param num_args The number of arguments in the `args` array.
 * @param return_value_out A pointer to an FFI_Argument struct where the return value should be stored.
 * Its `value_ptr` should point to a buffer of appropriate size. Can be NULL for void returns.
 * @return True if the function was invoked successfully, false otherwise.
 */
bool invoke_foreign_function(FFI_FunctionSignature* sig, FFI_Argument* args, int num_args, FFI_Argument* return_value_out) {
    void* actual_return_buffer_ptr = NULL;
    note("\n--- Inside invoke_foreign_function (FFI Gateway / Core VM) ---");
    note("FFI Gateway: Calling function '%s'. Args array: %p, Num args: %d, Return FFI_Argument: %p.",
           sig->debug_name, (void*)args, num_args, (void*)return_value_out);

    // --- Argument count validation moved to the top ---
    if (num_args != sig->num_params) {
        diag("Error: Incorrect number of arguments for function '%s'. Expected %d, got %d.",
                sig->debug_name, sig->num_params, num_args);
        return false;
    }
    // --- END Argument count validation ---

    if (return_value_out) {
        actual_return_buffer_ptr = return_value_out->value_ptr;
        note("FFI Gateway: Return value buffer pointer: %p", actual_return_buffer_ptr);
        if (!actual_return_buffer_ptr && sig->return_type != FFI_TYPE_VOID) {
            diag("Error: return_value_out->value_ptr is NULL for non-void return type. Cannot store result.");
            return false;
        }
    } else if (sig->return_type != FFI_TYPE_VOID) {
         diag("Warning: No return_value_out provided for non-void function '%s'. Return value will be lost.", sig->debug_name);
    }


    if (!sig->trampoline_code) {
        diag("Error: Trampoline code not generated/set for function '%s'.", sig->debug_name);
        return false;
    }

    // --- VERBOSE POINTER DEBUGGING ---
    note("FFI Gateway: Input FFI_Argument array address: %p", (void*)args);
    if (num_args > 0 && args != NULL) {
        for (int i = 0; i < num_args; ++i) {
            note("FFI Gateway:   args[%d].value_ptr = %p", i, args[i].value_ptr);
            // Attempt to peek at the value if it's an integer type (using sig->param_types[i])
            if (sig->param_types && i < sig->num_params) {
                FFI_Type arg_type = sig->param_types[i];
                note("FFI Gateway:   args[%d] (param_type %d) value: ", i, arg_type);
                if (args[i].value_ptr != NULL) {
                    switch (arg_type) {
                        case FFI_TYPE_BOOL:    note("%d (bool)", *(bool*)args[i].value_ptr); break;
                        case FFI_TYPE_CHAR:    note("%d (char)", *(char*)args[i].value_ptr); break;
                        case FFI_TYPE_UCHAR:   note("%u (uchar)", *(unsigned char*)args[i].value_ptr); break;
                        case FFI_TYPE_SCHAR:   note("%d (schar)", *(signed char*)args[i].value_ptr); break;
                        case FFI_TYPE_SHORT:   note("%hd (short)", *(short*)args[i].value_ptr); break;
                        case FFI_TYPE_USHORT:  note("%hu (ushort)", *(unsigned short*)args[i].value_ptr); break;
                        case FFI_TYPE_SSHORT:  note("%hd (sshort)", *(signed short*)args[i].value_ptr); break;
                        case FFI_TYPE_INT:     note("%d (int)", *(int*)args[i].value_ptr); break;
                        case FFI_TYPE_UINT:    note("%u (uint)", *(unsigned int*)args[i].value_ptr); break;
                        case FFI_TYPE_SINT:    note("%d (sint)", *(signed int*)args[i].value_ptr); break;
                        case FFI_TYPE_LONG:    note("%ld (long)", *(long*)args[i].value_ptr); break;
                        case FFI_TYPE_ULONG:   note("%lu (ulong)", *(unsigned long*)args[i].value_ptr); break;
                        case FFI_TYPE_SLONG:   note("%ld (slong)", *(signed long*)args[i].value_ptr); break;
                        case FFI_TYPE_LLONG:   note("%lld (llong)", *(long long*)args[i].value_ptr); break;
                        case FFI_TYPE_ULLONG:  note("%llu (ullong)", *(unsigned long long*)args[i].value_ptr); break;
                        case FFI_TYPE_SLLONG:  note("%lld (sllong)", *(signed long long*)args[i].value_ptr); break;
                        case FFI_TYPE_FLOAT:   note("%f (float)", *(float*)args[i].value_ptr); break;
                        case FFI_TYPE_DOUBLE:  note("%lf (double)", *(double*)args[i].value_ptr); break;
                        case FFI_TYPE_POINTER: note("%p (pointer)", *(void**)args[i].value_ptr); break;
                        case FFI_TYPE_WCHAR:   note("%lc (wchar_t)", *(wchar_t*)args[i].value_ptr); break;
                        case FFI_TYPE_SIZE_T:  note("%zu (size_t)", *(size_t*)args[i].value_ptr); break;
#if defined(__SIZEOF_INT128__) || defined(__GNUC__)
                        case FFI_TYPE_INT128:
                            {
                                __int128 val = *(__int128*)args[i].value_ptr;
                                note("0x%llx%016llx (__int128)", (unsigned long long)(val >> 64), (unsigned long long)val);
                            }
                            break;
                        case FFI_TYPE_UINT128:
                            {
                                unsigned __int128 val = *(unsigned __int128*)args[i].value_ptr;
                                note("0x%llx%016llx (unsigned __int128)", (unsigned long long)(val >> 64), (unsigned long long)val);
                            }
                            break;
#else
                        case FFI_TYPE_INT128:
                            {
                                int128_struct val = *(int128_struct*)args[i].value_ptr;
                                note("0x%llx%016llx (int128_struct)", (unsigned long long)val.high, (unsigned long long)val.low);
                            }
                            break;
                        case FFI_TYPE_UINT128:
                            {
                                uint128_struct val = *(uint128_struct*)args[i].value_ptr;
                                note("0x%llx%016llx (uint128_struct)", (unsigned long long)val.high, (unsigned long long)val.low);
                            }
                            break;
#endif
                        default: note("Unknown Type (at %p)", args[i].value_ptr); break;
                    }
                } else {
                    note("NULL pointer");
                }
            } else {
                note("FFI Gateway:   args[%d] value (raw ptr): %p", i, args[i].value_ptr);
            }
        }
    } else {
        note("FFI Gateway:   No arguments to display.");
    }
    // --- END VERBOSE POINTER DEBUGGING ---

    note("FFI Gateway: Final return buffer ptr passed to trampoline: %p", actual_return_buffer_ptr);

    // CRITICAL VALIDATION POINT: Ensure the return buffer pointer is valid before calling the trampoline
    if (sig->return_type != FFI_TYPE_VOID && actual_return_buffer_ptr == NULL) {
        diag("CRITICAL ERROR: Return buffer pointer is NULL for non-void function '%s'. This should have been caught earlier.", sig->debug_name);
        return false;
    }

    // (Additional sophisticated checks would involve ensuring it's writable memory, etc., but that's platform-dependent and usually handled by mmap PROT_WRITE)
    GenericTrampolinePtr trampoline = sig->trampoline_code;
    note("FFI Gateway: Calling dynamically generated generic trampoline for '%s' at %p...", sig->debug_name, (void*)trampoline);
    trampoline(args, num_args, actual_return_buffer_ptr);

    note("FFI Gateway: Trampoline finished. Function '%s' invoked successfully.", sig->debug_name);

    return true;
}

// --- Main Application ---
typedef union {
    bool b_val;
    char c_val;
    unsigned char uc_val;
    signed char sc_val; // For explicit signed char
    short s_val;
    unsigned short us_val;
    signed short ss_val; // For explicit signed short
    int i_val;
    unsigned int ui_val;
    signed int si_val;   // For explicit signed int
    long l_val;
    unsigned long ul_val;
    signed long sl_val;  // For explicit signed long
    long long ll_val;
    unsigned long long ull_val;
    signed long long sll_val; // For explicit signed long long
    float f_val;
    double d_val;
    void* ptr_val;
    wchar_t wc_val; // For wchar_t
    size_t sz_val;  // For size_t
#if defined(__SIZEOF_INT128__) || defined(__GNUC__)
    __int128 i128_val; // New: 128-bit signed
    unsigned __int128 ui128_val; // New: 128-bit unsigned
#else
    int128_struct i128_val; // Placeholder for 128-bit signed
    uint128_struct ui128_val; // Placeholder for 128-bit unsigned
#endif
    // Ensure this union is large enough for the largest possible return type (e.g., double or long long or 128-bit)
} GenericReturnValue;

// Global return value storage and FFI_Argument for tests
static FFI_Argument g_ffi_return_value;
static GenericReturnValue g_ret_storage;


// --- Test Functions (for subtest macro) ---

void test_get_fixed_int_minimal() {
    FFI_FunctionSignature* ffi_get_fixed_int_test = create_ffi_function(
        "get_fixed_int_minimal", FFI_TYPE_INT, 0, NULL, (GenericFuncPtr)get_fixed_int_minimal, NULL, 0);
    if (ffi_get_fixed_int_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        bool invocation_success = invoke_foreign_function(ffi_get_fixed_int_test, NULL, 0, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for get_fixed_int_minimal");
        is_int(g_ret_storage.i_val, 42, "Result (int): %d (Expected 42)", g_ret_storage.i_val);
        destroy_ffi_function(ffi_get_fixed_int_test);
    } else {
        fail("Failed to create FFI object for get_fixed_int_minimal.");
    }
}

void test_int_identity_minimal() {
    FFI_FunctionSignature* ffi_int_identity_test = create_ffi_function(
        "int_identity_minimal", FFI_TYPE_INT, 1, identity_int_params, (GenericFuncPtr)int_identity_minimal, NULL, 0);
    if (ffi_int_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        int identity_in_val = 123;
        FFI_Argument identity_args[] = { { .value_ptr = &identity_in_val } };
        bool invocation_success = invoke_foreign_function(ffi_int_identity_test, identity_args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for int_identity_minimal");
        is_int(g_ret_storage.i_val, identity_in_val, "Result (int): %d (Expected %d)", g_ret_storage.i_val, identity_in_val);
        destroy_ffi_function(ffi_int_identity_test);
    } else {
        fail("Failed to create FFI object for int_identity_minimal.");
    }
}

void test_bool_identity_minimal() {
    FFI_FunctionSignature* ffi_bool_identity_test = create_ffi_function(
        "bool_identity_minimal", FFI_TYPE_BOOL, 1, identity_bool_params, (GenericFuncPtr)bool_identity_minimal, NULL, 0);
    if (ffi_bool_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        bool in_val = true;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_bool_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for bool_identity_minimal");
        is_int(g_ret_storage.b_val, in_val, "Result (bool): %d (Expected %d)", g_ret_storage.b_val, in_val);
        destroy_ffi_function(ffi_bool_identity_test);
    } else {
        fail("Failed to create FFI object for bool_identity_minimal.");
    }
}

void test_char_identity_minimal() {
    FFI_FunctionSignature* ffi_char_identity_test = create_ffi_function(
        "char_identity_minimal", FFI_TYPE_CHAR, 1, identity_char_params, (GenericFuncPtr)char_identity_minimal, NULL, 0);
    if (ffi_char_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        char in_val = 'X';
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_char_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for char_identity_minimal");
        is_char(g_ret_storage.c_val, in_val, "Result (char): '%c' (Expected '%c')", g_ret_storage.c_val, in_val);
        destroy_ffi_function(ffi_char_identity_test);
    } else {
        fail("Failed to create FFI object for char_identity_minimal.");
    }
}

void test_uchar_identity_minimal() {
    FFI_FunctionSignature* ffi_uchar_identity_test = create_ffi_function(
        "uchar_identity_minimal", FFI_TYPE_UCHAR, 1, identity_uchar_params, (GenericFuncPtr)uchar_identity_minimal, NULL, 0);
    if (ffi_uchar_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        unsigned char in_val = 250;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_uchar_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for uchar_identity_minimal");
        is_int(g_ret_storage.uc_val, in_val, "Result (uchar): %u (Expected %u)", g_ret_storage.uc_val, in_val);
        destroy_ffi_function(ffi_uchar_identity_test);
    } else {
        fail("Failed to create FFI object for uchar_identity_minimal.");
    }
}

void test_short_identity_minimal() {
    FFI_FunctionSignature* ffi_short_identity_test = create_ffi_function(
        "short_identity_minimal", FFI_TYPE_SHORT, 1, identity_short_params, (GenericFuncPtr)short_identity_minimal, NULL, 0);
    if (ffi_short_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        short in_val = -32000;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_short_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for short_identity_minimal");
        is_int(g_ret_storage.s_val, in_val, "Result (short): %hd (Expected %hd)", g_ret_storage.s_val, in_val);
        destroy_ffi_function(ffi_short_identity_test);
    } else {
        fail("Failed to create FFI object for short_identity_minimal.");
    }
}

void test_ushort_identity_minimal() {
    FFI_FunctionSignature* ffi_ushort_identity_test = create_ffi_function(
        "ushort_identity_minimal", FFI_TYPE_USHORT, 1, identity_ushort_params, (GenericFuncPtr)ushort_identity_minimal, NULL, 0);
    if (ffi_ushort_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        unsigned short in_val = 65000;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_ushort_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for ushort_identity_minimal");
        is_int(g_ret_storage.us_val, in_val, "Result (ushort): %hu (Expected %hu)", g_ret_storage.us_val, in_val);
        destroy_ffi_function(ffi_ushort_identity_test);
    } else {
        fail("Failed to create FFI object for ushort_identity_minimal.");
    }
}

void test_long_identity_minimal() {
    FFI_FunctionSignature* ffi_long_identity_test = create_ffi_function(
        "long_identity_minimal", FFI_TYPE_LONG, 1, identity_long_params, (GenericFuncPtr)long_identity_minimal, NULL, 0);
    if (ffi_long_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        long in_val = 9876543210L; // Example long value (32-bit on Win64, 64-bit on Linux/macOS x64/ARM64)
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_long_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for long_identity_minimal");
        is_int(g_ret_storage.l_val, in_val, "Result (long): %ld (Expected %ld)", g_ret_storage.l_val, in_val);
        destroy_ffi_function(ffi_long_identity_test);
    } else {
        fail("Failed to create FFI object for long_identity_minimal.");
    }
}

void test_llong_identity_minimal() {
    FFI_FunctionSignature* ffi_llong_identity_test = create_ffi_function(
        "llong_identity_minimal", FFI_TYPE_LLONG, 1, identity_llong_params, (GenericFuncPtr)llong_identity_minimal, NULL, 0);
    if (ffi_llong_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        long long in_val = -9876543210987654321LL; // Example long long value
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_llong_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for llong_identity_minimal");
        is_int(g_ret_storage.ll_val, in_val, "Result (long long): %lld (Expected %lld)", g_ret_storage.ll_val, in_val);
        destroy_ffi_function(ffi_llong_identity_test);
    } else {
        fail("Failed to create FFI object for llong_identity_minimal.");
    }
}

void test_ullong_identity_minimal() {
    FFI_FunctionSignature* ffi_ullong_identity_test = create_ffi_function(
        "ullong_identity_minimal", FFI_TYPE_ULLONG, 1, identity_ullong_params, (GenericFuncPtr)ullong_identity_minimal, NULL, 0);
    if (ffi_ullong_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        unsigned long long in_val = 0xFEDCBA9876543210ULL; // Example ullong value
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_ullong_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for ullong_identity_minimal");
        is_int(g_ret_storage.ull_val, in_val, "Result (ullong): %llu (Expected %llu)", g_ret_storage.ull_val, in_val);
        destroy_ffi_function(ffi_ullong_identity_test);
    } else {
        fail("Failed to create FFI object for ullong_identity_minimal.");
    }
}

void test_float_identity_minimal() {
    FFI_FunctionSignature* ffi_float_identity_test = create_ffi_function(
        "float_identity_minimal", FFI_TYPE_FLOAT, 1, identity_float_params, (GenericFuncPtr)float_identity_minimal, NULL, 0);
    if (ffi_float_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        float in_val = 3.14159f;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_float_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for float_identity_minimal");
        is_float(g_ret_storage.f_val, in_val, "Result (float): %f (Expected %f)", g_ret_storage.f_val, in_val);
        destroy_ffi_function(ffi_float_identity_test);
    } else {
        fail("Failed to create FFI object for float_identity_minimal.");
    }
}

void test_double_identity_minimal() {
    FFI_FunctionSignature* ffi_double_identity_test = create_ffi_function(
        "double_identity_minimal", FFI_TYPE_DOUBLE, 1, identity_double_params, (GenericFuncPtr)double_identity_minimal, NULL, 0);
    if (ffi_double_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        double in_val = 2.718281828;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_double_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for double_identity_minimal");
        is_double(g_ret_storage.d_val, in_val, "Result (double): %lf (Expected %lf)", g_ret_storage.d_val, in_val);
        destroy_ffi_function(ffi_double_identity_test);
    } else {
        fail("Failed to create FFI object for double_identity_minimal.");
    }
}


void test_pointer_identity_minimal() {
    FFI_FunctionSignature* ffi_pointer_identity_test = create_ffi_function(
        "pointer_identity_minimal", FFI_TYPE_POINTER, 1, identity_pointer_params, (GenericFuncPtr)pointer_identity_minimal, NULL, 0);
    if (ffi_pointer_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        void* in_val = (void*)0xDEADBEEF; // Example pointer value
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_pointer_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for pointer_identity_minimal");
        is_ptr(g_ret_storage.ptr_val, in_val, "Result (pointer): %p (Expected %p)", g_ret_storage.ptr_val, in_val);
        destroy_ffi_function(ffi_pointer_identity_test);
    } else {
        fail("Failed to create FFI object for pointer_identity_minimal.");
    }
}

void test_print_two_ints() {
    FFI_FunctionSignature* ffi_print_two_ints_test = create_ffi_function(
        "print_two_ints", FFI_TYPE_VOID, 2, print_two_ints_params, (GenericFuncPtr)print_two_ints, NULL, 0);
    if (ffi_print_two_ints_test) {
        // No return value to clear for void function
        int int1_void = 50;
        int int2_void = 75;
        FFI_Argument two_ints_void_args[] = {
            { .value_ptr = &int1_void },
            { .value_ptr = &int2_void }
        };
        bool invocation_success = invoke_foreign_function(ffi_print_two_ints_test, two_ints_void_args, 2, NULL);
        ok(invocation_success, "FFI call successful for print_two_ints");
        destroy_ffi_function(ffi_print_two_ints_test);
    } else {
        fail("Failed to create FFI object for print_two_ints.");
    }
}

void test_print_float_and_double() {
    FFI_FunctionSignature* ffi_print_float_and_double_test = create_ffi_function(
        "print_float_and_double", FFI_TYPE_VOID, 2, print_float_double_params, (GenericFuncPtr)print_float_and_double, NULL, 0);
    if (ffi_print_float_and_double_test) {
        // No return value to clear for void function
        float f_test = 3.14f;
        double d_test = 2.718;
        FFI_Argument float_double_args[] = {
            { .value_ptr = &f_test },
            { .value_ptr = &d_test }
        };
        bool invocation_success = invoke_foreign_function(ffi_print_float_and_double_test, float_double_args, 2, NULL);
        ok(invocation_success, "FFI call successful for print_float_and_double");
        destroy_ffi_function(ffi_print_float_and_double_test);
    } else {
        fail("Failed to create FFI object for print_float_and_double.");
    }
}

void test_get_float_value() {
    FFI_FunctionSignature* ffi_get_float_value_test = create_ffi_function(
        "get_float_value", FFI_TYPE_FLOAT, 0, NULL, (GenericFuncPtr)get_float_value, NULL, 0);
    if (ffi_get_float_value_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        bool invocation_success = invoke_foreign_function(ffi_get_float_value_test, NULL, 0, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for get_float_value");
        is_float(g_ret_storage.f_val, 123.45f, "Result (float): %f (Expected 123.45)", g_ret_storage.f_val);
        destroy_ffi_function(ffi_get_float_value_test);
    } else {
        fail("Failed to create FFI object for get_float_value.");
    }
}

void test_get_double_value() {
    FFI_FunctionSignature* ffi_get_double_value_test = create_ffi_function(
        "get_double_value", FFI_TYPE_DOUBLE, 0, NULL, (GenericFuncPtr)get_double_value, NULL, 0);
    if (ffi_get_double_value_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        bool invocation_success = invoke_foreign_function(ffi_get_double_value_test, NULL, 0, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for get_double_value");
        is_double(g_ret_storage.d_val, 987.654, "Result (double): %lf (Expected 987.654)", g_ret_storage.d_val);
        destroy_ffi_function(ffi_get_double_value_test);
    } else {
        fail("Failed to create FFI object for get_double_value.");
    }
}

void test_sum_seven_ints() {
    FFI_FunctionSignature* ffi_sum_seven_ints_test = create_ffi_function(
        "sum_seven_ints", FFI_TYPE_INT, 7, sum_seven_ints_params, (GenericFuncPtr)sum_seven_ints, NULL, 0);
    if (ffi_sum_seven_ints_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        int v1=1, v2=2, v3=3, v4=4, v5=5, v6=6, v7=7;
        FFI_Argument args[] = {
            { .value_ptr = &v1 }, { .value_ptr = &v2 }, { .value_ptr = &v3 },
            { .value_ptr = &v4 }, { .value_ptr = &v5 }, { .value_ptr = &v6 },
            { .value_ptr = &v7 }
        };
        bool invocation_success = invoke_foreign_function(ffi_sum_seven_ints_test, args, 7, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for sum_seven_ints");
        is_int(g_ret_storage.i_val, 28, "Result (sum_seven_ints): %d (Expected 28)", g_ret_storage.i_val);
        destroy_ffi_function(ffi_sum_seven_ints_test);
    } else {
        fail("Failed to create FFI object for sum_seven_ints.");
    }
}

// NEW: Test for reentrancy of trampoline
void test_reentrancy_add_two_ints() {
    FFI_FunctionSignature* ffi_add_two_ints_reentrant = create_ffi_function(
        "add_two_ints_reentrant", FFI_TYPE_INT, 2, add_two_ints_params, (GenericFuncPtr)add_two_ints, NULL, 0);
    if (ffi_add_two_ints_reentrant) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;

        // Call 1
        int a1 = 10, b1 = 20;
        FFI_Argument args1[] = { { .value_ptr = &a1 }, { .value_ptr = &b1 } };
        bool success1 = invoke_foreign_function(ffi_add_two_ints_reentrant, args1, 2, &g_ffi_return_value);
        ok(success1, "Reentrancy Call 1 successful");
        is_int(g_ret_storage.i_val, 30, "Reentrancy Result 1: %d (Expected 30)", g_ret_storage.i_val);

        // Call 2
        int a2 = -5, b2 = 15;
        FFI_Argument args2[] = { { .value_ptr = &a2 }, { .value_ptr = &b2 } };
        bool success2 = invoke_foreign_function(ffi_add_two_ints_reentrant, args2, 2, &g_ffi_return_value);
        ok(success2, "Reentrancy Call 2 successful");
        is_int(g_ret_storage.i_val, 10, "Reentrancy Result 2: %d (Expected 10)", g_ret_storage.i_val);

        // Call 3
        int a3 = 100, b3 = -200;
        FFI_Argument args3[] = { { .value_ptr = &a3 }, { .value_ptr = &b3 } };
        bool success3 = invoke_foreign_function(ffi_add_two_ints_reentrant, args3, 2, &g_ffi_return_value);
        ok(success3, "Reentrancy Call 3 successful");
        is_int(g_ret_storage.i_val, -100, "Reentrancy Result 3: %d (Expected -100)", g_ret_storage.i_val);

        destroy_ffi_function(ffi_add_two_ints_reentrant);
    } else {
        fail("Failed to create FFI object for reentrancy test.");
    }
}

// NEW: Test for mixed arguments: int, float, pointer
void test_mixed_args_int_float_ptr() {
    FFI_FunctionSignature* ffi_mixed_test1 = create_ffi_function(
        "mixed_int_float_ptr_func", FFI_TYPE_INT, 3, mixed_int_float_ptr_params, (GenericFuncPtr)mixed_int_float_ptr_func, NULL, 0);
    if (ffi_mixed_test1) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;

        int i_val = 10;
        float f_val = 5.5f;
        void* ptr_val = (void*)0x12345678; // Non-NULL pointer
        FFI_Argument args[] = {
            { .value_ptr = &i_val },
            { .value_ptr = &f_val },
            { .value_ptr = &ptr_val }
        };
        bool success = invoke_foreign_function(ffi_mixed_test1, args, 3, &g_ffi_return_value);
        ok(success, "Mixed args (int,float,ptr) call successful");
        // Expected: 10 + (int)5.5f + (1 if ptr_val != NULL) = 10 + 5 + 1 = 16
        is_int(g_ret_storage.i_val, 16, "Mixed args (int,float,ptr) result: %d (Expected 16)", g_ret_storage.i_val);

        destroy_ffi_function(ffi_mixed_test1);
    } else {
        fail("Failed to create FFI object for mixed_int_float_ptr_func.");
    }
}

// NEW: Test for mixed arguments: double, char, int
void test_mixed_args_double_char_int() {
    FFI_FunctionSignature* ffi_mixed_test2 = create_ffi_function(
        "mixed_double_char_int_func", FFI_TYPE_DOUBLE, 3, mixed_double_char_int_params, (GenericFuncPtr)mixed_double_char_int_func, NULL, 0);
    if (ffi_mixed_test2) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;

        double d_val = 10.5;
        char c_val = 'A'; // ASCII 65
        int i_val = 20;
        FFI_Argument args[] = {
            { .value_ptr = &d_val },
            { .value_ptr = &c_val },
            { .value_ptr = &i_val }
        };
        bool success = invoke_foreign_function(ffi_mixed_test2, args, 3, &g_ffi_return_value);
        ok(success, "Mixed args (double,char,int) call successful");
        // Expected: 10.5 + 65.0 + 20.0 = 95.5
        is_double(g_ret_storage.d_val, 95.5, "Mixed args (double,char,int) result: %lf (Expected 95.5)", g_ret_storage.d_val);

        destroy_ffi_function(ffi_mixed_test2);
    } else {
        fail("Failed to create FFI object for mixed_double_char_int_func.");
    }
}

// NEW: Reentrancy test with stack spilling for GPR arguments
void test_reentrancy_sum_eight_ints() {
    FFI_FunctionSignature* ffi_sum_eight_ints_reentrant = create_ffi_function(
        "sum_eight_ints_reentrant", FFI_TYPE_INT, 8, sum_eight_ints_params, (GenericFuncPtr)sum_eight_ints, NULL, 0);
    if (ffi_sum_eight_ints_reentrant) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;

        // Call 1: All args sum to 36
        int v1_1=1, v1_2=2, v1_3=3, v1_4=4, v1_5=5, v1_6=6, v1_7=7, v1_8=8;
        FFI_Argument args1[] = {
            { .value_ptr = &v1_1 }, { .value_ptr = &v1_2 }, { .value_ptr = &v1_3 },
            { .value_ptr = &v1_4 }, { .value_ptr = &v1_5 }, { .value_ptr = &v1_6 },
            { .value_ptr = &v1_7 }, { .value_ptr = &v1_8 }
        };
        bool success1 = invoke_foreign_function(ffi_sum_eight_ints_reentrant, args1, 8, &g_ffi_return_value);
        ok(success1, "Reentrancy sum_eight_ints Call 1 successful");
        is_int(g_ret_storage.i_val, 36, "Reentrancy sum_eight_ints Result 1: %d (Expected 36)", g_ret_storage.i_val);

        // Call 2: All args sum to 0
        int v2_1=0, v2_2=0, v2_3=0, v2_4=0, v2_5=0, v2_6=0, v2_7=0, v2_8=0;
        FFI_Argument args2[] = {
            { .value_ptr = &v2_1 }, { .value_ptr = &v2_2 }, { .value_ptr = &v2_3 },
            { .value_ptr = &v2_4 }, { .value_ptr = &v2_5 }, { .value_ptr = &v2_6 },
            { .value_ptr = &v2_7 }, { .value_ptr = &v2_8 }
        };
        bool success2 = invoke_foreign_function(ffi_sum_eight_ints_reentrant, args2, 8, &g_ffi_return_value);
        ok(success2, "Reentrancy sum_eight_ints Call 2 successful");
        is_int(g_ret_storage.i_val, 0, "Reentrancy sum_eight_ints Result 2: %d (Expected 0)", g_ret_storage.i_val);

        // Call 3: Mixed positive/negative, sum to 100
        int v3_1=10, v3_2=20, v3_3=30, v3_4=40, v3_5=50, v3_6=-10, v3_7=-20, v3_8=-30;
        FFI_Argument args3[] = {
            { .value_ptr = &v3_1 }, { .value_ptr = &v3_2 }, { .value_ptr = &v3_3 },
            { .value_ptr = &v3_4 }, { .value_ptr = &v3_5 }, { .value_ptr = &v3_6 },
            { .value_ptr = &v3_7 }, { .value_ptr = &v3_8 }
        };
        bool success3 = invoke_foreign_function(ffi_sum_eight_ints_reentrant, args3, 8, &g_ffi_return_value);
        ok(success3, "Reentrancy sum_eight_ints Call 3 successful");
        is_int(g_ret_storage.i_val, 90, "Reentrancy sum_eight_ints Result 3: %d (Expected 90)", g_ret_storage.i_val); // 10+20+30+40+50-10-20-30 = 90

        destroy_ffi_function(ffi_sum_eight_ints_reentrant);
    } else {
        fail("Failed to create FFI object for reentrancy sum_eight_ints test.");
    }
}

// NEW: Reentrancy test with stack spilling for XMM arguments
void test_reentrancy_sum_nine_doubles() {
    FFI_FunctionSignature* ffi_sum_nine_doubles_reentrant = create_ffi_function(
        "sum_nine_doubles_reentrant", FFI_TYPE_DOUBLE, 9, sum_nine_doubles_params, (GenericFuncPtr)sum_nine_doubles, NULL, 0);
    if (ffi_sum_nine_doubles_reentrant) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;

        // Call 1
        double d1=1.0, d2=2.0, d3=3.0, d4=4.0, d5=5.0, d6=6.0, d7=7.0, d8=8.0, d9=9.0;
        FFI_Argument args1[] = {
            { .value_ptr = &d1 }, { .value_ptr = &d2 }, { .value_ptr = &d3 },
            { .value_ptr = &d4 }, { .value_ptr = &d5 }, { .value_ptr = &d6 },
            { .value_ptr = &d7 }, { .value_ptr = &d8 }, { .value_ptr = &d9 }
        };
        bool success1 = invoke_foreign_function(ffi_sum_nine_doubles_reentrant, args1, 9, &g_ffi_return_value);
        ok(success1, "Reentrancy Call 1 (9 doubles) successful");
        is_double(g_ret_storage.d_val, 45.0, "Reentrancy Result 1 (9 doubles): %lf (Expected 45.0)", g_ret_storage.d_val);

        // Call 2
        double e1=10.0, e2=20.0, e3=30.0, e4=40.0, e5=50.0, e6=60.0, e7=70.0, e8=80.0, e9=90.0;
        FFI_Argument args2[] = {
            { .value_ptr = &e1 }, { .value_ptr = &e2 }, { .value_ptr = &e3 },
            { .value_ptr = &e4 }, { .value_ptr = &e5 }, { .value_ptr = &e6 },
            { .value_ptr = &e7 }, { .value_ptr = &e8 }, { .value_ptr = &e9 }
        };
        bool success2 = invoke_foreign_function(ffi_sum_nine_doubles_reentrant, args2, 9, &g_ffi_return_value);
        ok(success2, "Reentrancy Call 2 (9 doubles) successful");
        is_double(g_ret_storage.d_val, 450.0, "Reentrancy Result 2 (9 doubles): %lf (Expected 450.0)", g_ret_storage.d_val);

        destroy_ffi_function(ffi_sum_nine_doubles_reentrant);
    } else {
        fail("Failed to create FFI object for reentrancy test (9 doubles).");
    }
}

// NEW: Test for mixed GPR and XMM arguments with stack spilling
void test_mixed_gpr_xmm_stack_spill() {
    FFI_FunctionSignature* ffi_mixed_spill_test = create_ffi_function(
        "mixed_gpr_xmm_stack_spill_func", FFI_TYPE_INT, 16, mixed_gpr_xmm_stack_spill_params, (GenericFuncPtr)mixed_gpr_xmm_stack_spill_func, NULL, 0);
    if (ffi_mixed_spill_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;

        int i1=1, i2=2, i3=3, i4=4, i5=5, i6=6;
        float f1=1.0f, f2=2.0f, f3=3.0f, f4=4.0f, f5=5.0f, f6=6.0f, f7=7.0f, f8=8.0f;
        int i7=7; // This will spill
        double d9=9.0; // This will spill

        FFI_Argument args[] = {
            { .value_ptr = &i1 }, { .value_ptr = &i2 }, { .value_ptr = &i3 }, { .value_ptr = &i4 }, { .value_ptr = &i5 }, { .value_ptr = &i6 },
            { .value_ptr = &f1 }, { .value_ptr = &f2 }, { .value_ptr = &f3 }, { .value_ptr = &f4 }, { .value_ptr = &f5 }, { .value_ptr = &f6 }, { .value_ptr = &f7 }, { .value_ptr = &f8 },
            { .value_ptr = &i7 },
            { .value_ptr = &d9 }
        };

        bool success = invoke_foreign_function(ffi_mixed_spill_test, args, 16, &g_ffi_return_value);
        ok(success, "Mixed GPR/XMM stack spill call successful");

        // Expected sum:
        // GPRs: 1+2+3+4+5+6 = 21
        // XMMs (as ints): 1+2+3+4+5+6+7+8 = 36
        // Stack: 7 + 9 = 16
        // Total: 21 + 36 + 16 = 73
        is_int(g_ret_storage.i_val, 73, "Mixed GPR/XMM with stack spill result: %d (Expected 73)", g_ret_storage.i_val);

        destroy_ffi_function(ffi_mixed_spill_test);
    } else {
        fail("Failed to create FFI object for mixed_gpr_xmm_stack_spill_func.");
    }
}

// NEW: Test for int_identity_minimal with INT_MIN and INT_MAX
void test_int_identity_min_max() {
    FFI_FunctionSignature* ffi_int_identity_test = create_ffi_function(
        "int_identity_minimal", FFI_TYPE_INT, 1, identity_int_params, (GenericFuncPtr)int_identity_minimal, NULL, 0);
    if (ffi_int_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;

        int min_val = INT_MIN;
        FFI_Argument args_min[] = { { .value_ptr = &min_val } };
        bool success_min = invoke_foreign_function(ffi_int_identity_test, args_min, 1, &g_ffi_return_value);
        ok(success_min, "FFI call successful for int_identity_minimal (INT_MIN)");
        is_int(g_ret_storage.i_val, INT_MIN, "Result (int_MIN): %d (Expected %d)", g_ret_storage.i_val, INT_MIN);

        int max_val = INT_MAX;
        FFI_Argument args_max[] = { { .value_ptr = &max_val } };
        bool success_max = invoke_foreign_function(ffi_int_identity_test, args_max, 1, &g_ffi_return_value);
        ok(success_max, "FFI call successful for int_identity_minimal (INT_MAX)");
        is_int(g_ret_storage.i_val, INT_MAX, "Result (int_MAX): %d (Expected %d)", g_ret_storage.i_val, INT_MAX);

        destroy_ffi_function(ffi_int_identity_test);
    } else {
        fail("Failed to create FFI object for int_identity_min_max.");
    }
}

// NEW: Test for float_identity_minimal with FLT_MIN and FLT_MAX
void test_float_identity_min_max() {
    FFI_FunctionSignature* ffi_float_identity_test = create_ffi_function(
        "float_identity_minimal", FFI_TYPE_FLOAT, 1, identity_float_params, (GenericFuncPtr)float_identity_minimal, NULL, 0);
    if (ffi_float_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;

        float min_val = FLT_MIN;
        FFI_Argument args_min[] = { { .value_ptr = &min_val } };
        bool success_min = invoke_foreign_function(ffi_float_identity_test, args_min, 1, &g_ffi_return_value);
        ok(success_min, "FFI call successful for float_identity_minimal (FLT_MIN)");
        is_float(g_ret_storage.f_val, FLT_MIN, "Result (float_MIN): %f (Expected %f)", g_ret_storage.f_val, FLT_MIN);

        float max_val = FLT_MAX;
        FFI_Argument args_max[] = { { .value_ptr = &max_val } };
        bool success_max = invoke_foreign_function(ffi_float_identity_test, args_max, 1, &g_ffi_return_value);
        ok(success_max, "FFI call successful for float_identity_minimal (FLT_MAX)");
        is_float(g_ret_storage.f_val, FLT_MAX, "Result (float_MAX): %f (Expected %f)", g_ret_storage.f_val, FLT_MAX);

        destroy_ffi_function(ffi_float_identity_test);
    } else {
        fail("Failed to create FFI object for float_identity_min_max.");
    }
}

// NEW: Test for double_identity_minimal with DBL_MIN and DBL_MAX
void test_double_identity_min_max() {
    FFI_FunctionSignature* ffi_double_identity_test = create_ffi_function(
        "double_identity_minimal", FFI_TYPE_DOUBLE, 1, identity_double_params, (GenericFuncPtr)double_identity_minimal, NULL, 0);
    if (ffi_double_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;

        double min_val = DBL_MIN;
        FFI_Argument args_min[] = { { .value_ptr = &min_val } };
        bool success_min = invoke_foreign_function(ffi_double_identity_test, args_min, 1, &g_ffi_return_value);
        ok(success_min, "FFI call successful for double_identity_minimal (DBL_MIN)");
        is_double(g_ret_storage.d_val, DBL_MIN, "Result (double_MIN): %lf (Expected %lf)", g_ret_storage.d_val, DBL_MIN);

        double max_val = DBL_MAX;
        FFI_Argument args_max[] = { { .value_ptr = &max_val } };
        bool success_max = invoke_foreign_function(ffi_double_identity_test, args_max, 1, &g_ffi_return_value);
        ok(success_max, "FFI call successful for double_identity_minimal (DBL_MAX)");
        is_double(g_ret_storage.d_val, DBL_MAX, "Result (double_MAX): %lf (Expected %lf)", g_ret_storage.d_val, DBL_MAX);

        destroy_ffi_function(ffi_double_identity_test);
    } else {
        fail("Failed to create FFI object for double_identity_min_max.");
    }
}

// NEW: Test for return_constant_42 (int, int -> int, no printf)
void test_return_constant_42() {
    FFI_FunctionSignature* ffi_return_constant_42_test = create_ffi_function(
        "return_constant_42", FFI_TYPE_INT, 2, add_two_ints_params, (GenericFuncPtr)return_constant_42, NULL, 0);
    if (ffi_return_constant_42_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        int val1 = 10, val2 = 20;
        FFI_Argument args[] = { { .value_ptr = &val1 }, { .value_ptr = &val2 } };
        bool success = invoke_foreign_function(ffi_return_constant_42_test, args, 2, &g_ffi_return_value);
        ok(success, "FFI call successful for return_constant_42");
        is_int(g_ret_storage.i_val, 42, "Result (return_constant_42): %d (Expected 42)", g_ret_storage.i_val);
        destroy_ffi_function(ffi_return_constant_42_test);
    } else {
        fail("Failed to create FFI object for return_constant_42.");
    }
}

// NEW: Test for pointer_identity_minimal with NULL
void test_pointer_identity_null() {
    FFI_FunctionSignature* ffi_pointer_identity_test = create_ffi_function(
        "pointer_identity_minimal", FFI_TYPE_POINTER, 1, identity_pointer_params, (GenericFuncPtr)pointer_identity_minimal, NULL, 0);
    if (ffi_pointer_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;

        void* null_ptr = NULL;
        FFI_Argument args[] = { { .value_ptr = &null_ptr } };
        bool invocation_success = invoke_foreign_function(ffi_pointer_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for pointer_identity_minimal (NULL)");
        is_ptr(g_ret_storage.ptr_val, NULL, "Result (pointer_NULL): %p (Expected NULL)", g_ret_storage.ptr_val);
        destroy_ffi_function(ffi_pointer_identity_test);
    } else {
        fail("Failed to create FFI object for pointer_identity_null.");
    }
}

// NEW: Test for all zero arguments for sum_seven_ints
void test_all_zero_args_sum_seven_ints() {
    FFI_FunctionSignature* ffi_sum_seven_ints_test = create_ffi_function(
        "sum_seven_ints", FFI_TYPE_INT, 7, sum_seven_ints_params, (GenericFuncPtr)sum_seven_ints, NULL, 0);
    if (ffi_sum_seven_ints_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        int v1=0, v2=0, v3=0, v4=0, v5=0, v6=0, v7=0;
        FFI_Argument args[] = {
            { .value_ptr = &v1 }, { .value_ptr = &v2 }, { .value_ptr = &v3 },
            { .value_ptr = &v4 }, { .value_ptr = &v5 }, { .value_ptr = &v6 },
            { .value_ptr = &v7 }
        };
        bool invocation_success = invoke_foreign_function(ffi_sum_seven_ints_test, args, 7, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for sum_seven_ints (all zeros)");
        is_int(g_ret_storage.i_val, 0, "Result (sum_seven_ints all zeros): %d (Expected 0)", g_ret_storage.i_val);
        destroy_ffi_function(ffi_sum_seven_ints_test);
    } else {
        fail("Failed to create FFI object for sum_seven_ints (all zeros).");
    }
}
// NEW: Test for wchar_t identity
void test_wchar_t_identity_minimal() {
    FFI_FunctionSignature* ffi_wchar_t_identity_test = create_ffi_function(
        "wchar_t_identity_minimal", FFI_TYPE_WCHAR, 1, identity_wchar_params, (GenericFuncPtr)wchar_t_identity_minimal, NULL, 0);
    if (ffi_wchar_t_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        wchar_t in_val = L'€'; // Euro sign, typically 3 bytes in UTF-8, but single wchar_t
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool success = invoke_foreign_function(ffi_wchar_t_identity_test, args, 1, &g_ffi_return_value);
        ok(success, "FFI call successful for wchar_t_identity_minimal");
        is_wchar_t(g_ret_storage.wc_val, in_val, "Result (wchar_t): %lc (Expected %lc)", g_ret_storage.wc_val, in_val);
        destroy_ffi_function(ffi_wchar_t_identity_test);
    } else {
        fail("Failed to create FFI object for wchar_t_identity_minimal.");
    }
}

// NEW: Test for size_t identity
void test_size_t_identity_minimal() {
    FFI_FunctionSignature* ffi_size_t_identity_test = create_ffi_function(
        "size_t_identity_minimal", FFI_TYPE_SIZE_T, 1, identity_size_t_params, (GenericFuncPtr)size_t_identity_minimal, NULL, 0);
    if (ffi_size_t_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        size_t in_val = (size_t)0xABCD12345678ULL; // Example size_t value
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool success = invoke_foreign_function(ffi_size_t_identity_test, args, 1, &g_ffi_return_value);
        ok(success, "FFI call successful for size_t_identity_minimal");
        is_size_t(g_ret_storage.sz_val, in_val, "Result (size_t): %zu (Expected %zu)", g_ret_storage.sz_val, in_val);
        destroy_ffi_function(ffi_size_t_identity_test);
    } else {
        fail("Failed to create FFI object for size_t_identity_minimal.");
    }
}

// NEW: Test for signed char identity
void test_schar_identity_minimal() {
    FFI_FunctionSignature* ffi_schar_identity_test = create_ffi_function(
        "schar_identity_minimal", FFI_TYPE_SCHAR, 1, identity_schar_params, (GenericFuncPtr)schar_identity_minimal, NULL, 0);
    if (ffi_schar_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        signed char in_val = -120;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool success = invoke_foreign_function(ffi_schar_identity_test, args, 1, &g_ffi_return_value);
        ok(success, "FFI call successful for schar_identity_minimal");
        is_int(g_ret_storage.sc_val, in_val, "Result (schar): %d (Expected %d)", g_ret_storage.sc_val, in_val);
        destroy_ffi_function(ffi_schar_identity_test);
    } else {
        fail("Failed to create FFI object for schar_identity_minimal.");
    }
}

// NEW: Test for signed short identity
void test_sshort_identity_minimal() {
    FFI_FunctionSignature* ffi_sshort_identity_test = create_ffi_function(
        "sshort_identity_minimal", FFI_TYPE_SSHORT, 1, identity_sshort_params, (GenericFuncPtr)sshort_identity_minimal, NULL, 0);
    if (ffi_sshort_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        signed short in_val = -30000;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool success = invoke_foreign_function(ffi_sshort_identity_test, args, 1, &g_ffi_return_value);
        ok(success, "FFI call successful for sshort_identity_minimal");
        is_int(g_ret_storage.ss_val, in_val, "Result (signed short): %hd (Expected %hd)", g_ret_storage.ss_val, in_val);
        destroy_ffi_function(ffi_sshort_identity_test);
    } else {
        fail("Failed to create FFI object for sshort_identity_minimal.");
    }
}

// NEW: Test for signed int identity
void test_sint_identity_minimal() {
    FFI_FunctionSignature* ffi_sint_identity_test = create_ffi_function(
        "sint_identity_minimal", FFI_TYPE_SINT, 1, identity_sint_params, (GenericFuncPtr)sint_identity_minimal, NULL, 0);
    if (ffi_sint_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        signed int in_val = -2000000000;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool success = invoke_foreign_function(ffi_sint_identity_test, args, 1, &g_ffi_return_value);
        ok(success, "FFI call successful for sint_identity_minimal");
        is_int(g_ret_storage.si_val, in_val, "Result (signed int): %d (Expected %d)", g_ret_storage.si_val, in_val);
        destroy_ffi_function(ffi_sint_identity_test);
    } else {
        fail("Failed to create FFI object for sint_identity_minimal.");
    }
}

// NEW: Test for signed long identity
void test_slong_identity_minimal() {
    FFI_FunctionSignature* ffi_slong_identity_test = create_ffi_function(
        "slong_identity_minimal", FFI_TYPE_SLONG, 1, identity_slong_params, (GenericFuncPtr)slong_identity_minimal, NULL, 0);
    if (ffi_slong_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        signed long in_val = -9000000000000000000L;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool invocation_success = invoke_foreign_function(ffi_slong_identity_test, args, 1, &g_ffi_return_value);
        ok(invocation_success, "FFI call successful for slong_identity_minimal");
        is_int(g_ret_storage.sl_val, in_val, "Result (signed long): %ld (Expected %ld)", g_ret_storage.sl_val, in_val);
        destroy_ffi_function(ffi_slong_identity_test);
    } else {
        fail("Failed to create FFI object for slong_identity_minimal.");
    }
}

// NEW: Test for signed long long identity
void test_sllong_identity_minimal() {
    FFI_FunctionSignature* ffi_sllong_identity_test = create_ffi_function(
        "sllong_identity_minimal", FFI_TYPE_SLLONG, 1, identity_sllong_params, (GenericFuncPtr)sllong_identity_minimal, NULL, 0);
    if (ffi_sllong_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        signed long long in_val = -987654321098765432LL;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool success = invoke_foreign_function(ffi_sllong_identity_test, args, 1, &g_ffi_return_value);
        ok(success, "FFI call successful for sllong_identity_minimal");
        is_int(g_ret_storage.sll_val, in_val, "Result (sllong): %lld (Expected %lld)", g_ret_storage.sll_val, in_val);
        destroy_ffi_function(ffi_sllong_identity_test);
    } else {
        fail("Failed to create FFI object for sllong_identity_minimal.");
    }
}

// NEW: Test for int128 identity
void test_int128_identity_minimal() {
#if defined(__SIZEOF_INT128__) || defined(__GNUC__)
    FFI_FunctionSignature* ffi_int128_identity_test = create_ffi_function(
        "int128_identity_minimal", FFI_TYPE_INT128, 1, identity_int128_params, (GenericFuncPtr)int128_identity_minimal, NULL, 0);
    if (ffi_int128_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        __int128 in_val = (__int128)0x1234567890ABCDEFLL << 64 | 0xFEDCBA9876543210LL;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool success = invoke_foreign_function(ffi_int128_identity_test, args, 1, &g_ffi_return_value);
        ok(success, "FFI call successful for int128_identity_minimal");
        is_int128(g_ret_storage.i128_val, in_val, "Result (int128): 0x%llx%016llx (Expected 0x%llx%016llx)",
                  (unsigned long long)(g_ret_storage.i128_val >> 64), (unsigned long long)g_ret_storage.i128_val,
                  (unsigned long long)(in_val >> 64), (unsigned long long)in_val);
        destroy_ffi_function(ffi_int128_identity_test);
    } else {
        fail("Failed to create FFI object for int128_identity_minimal.");
    }
#else
    skip("int128_identity_minimal skipped: __int128 not supported on this compiler.");
#endif
}

// NEW: Test for uint128 identity
void test_uint128_identity_minimal() {
#if defined(__SIZEOF_INT128__) || defined(__GNUC__)
    FFI_FunctionSignature* ffi_uint128_identity_test = create_ffi_function(
        "uint128_identity_minimal", FFI_TYPE_UINT128, 1, identity_uint128_params, (GenericFuncPtr)uint128_identity_minimal, NULL, 0);
    if (ffi_uint128_identity_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage));
        g_ffi_return_value.value_ptr = &g_ret_storage;
        unsigned __int128 in_val = (unsigned __int128)0xFEDCBA9876543210ULL << 64 | 0x1234567890ABCDEFULL;
        FFI_Argument args[] = { { .value_ptr = &in_val } };
        bool success = invoke_foreign_function(ffi_uint128_identity_test, args, 1, &g_ffi_return_value);
        ok(success, "FFI call successful for uint128_identity_minimal");
        is_uint128(g_ret_storage.ui128_val, in_val, "Result (uint128): 0x%llx%016llx (Expected 0x%llx%016llx)",
                   (unsigned long long)(g_ret_storage.ui128_val >> 64), (unsigned long long)g_ret_storage.ui128_val,
                   (unsigned long long)(in_val >> 64), (unsigned long long)in_val);
        destroy_ffi_function(ffi_uint128_identity_test);
    } else {
        fail("Failed to create FFI object for uint128_identity_minimal.");
    }
#else
    skip("uint128_identity_minimal skipped: __int128 not supported on this compiler.");
#endif
}

void test_error_case_add_two_ints_wrong_arg_count() {
    FFI_FunctionSignature* ffi_add_two_ints_test = create_ffi_function(
        "add_two_ints", FFI_TYPE_INT, 2, add_two_ints_params, (GenericFuncPtr)add_two_ints, NULL, 0);
    if (ffi_add_two_ints_test) {
        memset(&g_ret_storage, 0, sizeof(g_ret_storage)); // Clear return storage
        g_ffi_return_value.value_ptr = &g_ret_storage; // Ensure return value pointer is set
        int i_error_val = 5;
        FFI_Argument args_error1[] = { { .value_ptr = &i_error_val } };
        bool invocation_success = invoke_foreign_function(ffi_add_two_ints_test, args_error1, 1, &g_ffi_return_value);
        // This call is expected to fail due to argument count mismatch
        ok(!invocation_success, "Invocation expected to fail due to incorrect argument count.");
        destroy_ffi_function(ffi_add_two_ints_test);
    } else {
        skip("Skipping error case test for add_two_ints (dependent FFI object not created).");
    }
}

int main() {
    plan(54); // Total number of subtests

    note("Starting main application with runtime assembly generation example (object-oriented FFI).");
    note("sizeof(FFI_Argument): %zu", sizeof(FFI_Argument)); // Should be 8 on x86-64 now

    // Initialize global return value storage
    g_ffi_return_value.value_ptr = &g_ret_storage;

    subtest("int get_fixed_int_minimal()", test_get_fixed_int_minimal);
 subtest("int noargs_intreturn(void)", test_get_fixed_int_minimal);
    subtest("int int_in_out(int)", test_int_identity_minimal);
    subtest("bool bool_in_out(bool)", test_bool_identity_minimal);
    subtest("char char_in_out(char)", test_char_identity_minimal);
    subtest("unsigned char uchar_in_out(unsigned char)", test_uchar_identity_minimal);
    subtest("short short_in_out(short)", test_short_identity_minimal);
    subtest("unsigned short ushort_in_out(unsigned short)", test_ushort_identity_minimal);
    subtest("long long_in_out(long)", test_long_identity_minimal);
    subtest("long long llong_in_out(long long)", test_llong_identity_minimal);
    subtest("unsigned long long ullong_in_out(unsigned long long)", test_ullong_identity_minimal);
    subtest("float float_in_out(float)", test_float_identity_minimal);
    subtest("double double_in_out(double)", test_double_identity_minimal);
    subtest("void* pointer_in_out(void*)", test_pointer_identity_minimal);
    subtest("void print_two_ints(int, int)", test_print_two_ints);
    subtest("void print_float_and_double(float, double)", test_print_float_and_double);
    subtest("float get_float_value()", test_get_float_value);
    subtest("double get_double_value()", test_get_double_value);
    subtest("int sum_seven_ints(int, int, int, int, int, int, int)", test_sum_seven_ints);
    subtest("Reentrancy test for add_two_ints", test_reentrancy_add_two_ints);
    subtest("Mixed args (int, float, ptr)", test_mixed_args_int_float_ptr);
    subtest("Mixed args (double, char, int)", test_mixed_args_double_char_int);
    subtest("Reentrancy test for sum_eight_ints", test_reentrancy_sum_eight_ints);
    subtest("Reentrancy test for sum_nine_doubles", test_reentrancy_sum_nine_doubles);
    subtest("Mixed GPR/XMM stack spill test", test_mixed_gpr_xmm_stack_spill);
    subtest("int return_constant_42(int, int)", test_return_constant_42); // Added new test
    subtest("wchar_t wchar_t_in_out(wchar_t)", test_wchar_t_identity_minimal); // Added new test
    subtest("size_t size_t_in_out(size_t)", test_size_t_identity_minimal); // Added new test
    subtest("signed char schar_in_out(signed char)", test_schar_identity_minimal); // Added new test
    subtest("signed short sshort_in_out(signed short)", test_sshort_identity_minimal); // Added new test
    subtest("signed int sint_in_out(signed int)", test_sint_identity_minimal); // Added new test
    subtest("signed long slong_in_out(signed long)", test_slong_identity_minimal); // Added new test
    subtest("signed long long sllong_in_out(signed long long)", test_sllong_identity_minimal); // Added new test
    subtest("__int128 int128_in_out(__int128)", test_int128_identity_minimal); // Added new test
    subtest("unsigned __int128 uint128_in_out(unsigned __int128)", test_uint128_identity_minimal); // Added new test
    subtest("Trampoline Reentrancy: add_two_ints multiple calls", test_reentrancy_add_two_ints);
    subtest("Mixed Arguments: int, float, pointer", test_mixed_args_int_float_ptr);
    subtest("Mixed Arguments: double, char, int", test_mixed_args_double_char_int);
    subtest("Trampoline Reentrancy with Stack Spill: sum_eight_ints", test_reentrancy_sum_eight_ints);
    // subtest("Long GPR List (8 ints, 2 on stack)", test_long_gpr_list);
    // subtest("Long XMM List (9 doubles, 1 on stack)", test_long_xmm_list);
    subtest("Long Mixed List (GPRs, XMMs, and stack spill)", test_mixed_gpr_xmm_stack_spill);
    subtest("int_identity_minimal (INT_MIN/MAX)", test_int_identity_min_max);
    subtest("float_identity_minimal (FLT_MIN/MAX)", test_float_identity_min_max);
    subtest("double_identity_minimal (DBL_MIN/MAX)", test_double_identity_min_max);
    subtest("pointer_identity_minimal (NULL)", test_pointer_identity_null);
    subtest("sum_seven_ints (all zeros)", test_all_zero_args_sum_seven_ints);

    note("\n--- Running New Type Tests ---\n");
    subtest("wchar_t wchar_t_in_out(wchar_t)", test_wchar_t_identity_minimal);
    subtest("size_t size_t_in_out(size_t)", test_size_t_identity_minimal);
    subtest("signed char schar_in_out(signed char)", test_schar_identity_minimal);
    subtest("signed short sshort_in_out(signed short)", test_sshort_identity_minimal);
    subtest("signed int sint_in_out(signed int)", test_sint_identity_minimal);
    subtest("signed long slong_in_out(signed long)", test_slong_identity_minimal);
    subtest("signed long long sllong_in_out(signed long long)", test_sllong_identity_minimal);
    subtest("__int128 int128_in_out(__int128)", test_int128_identity_minimal); // New 128-bit test
    subtest("unsigned __int128 uint128_in_out(unsigned __int128)", test_uint128_identity_minimal); // New 128-bit test


    return done_testing(); // Marks the end of tests

}
