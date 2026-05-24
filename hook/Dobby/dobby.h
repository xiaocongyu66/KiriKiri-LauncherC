#ifndef dobby_h
#define dobby_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef uintptr_t addr_t;
typedef uint32_t addr32_t;
typedef uint64_t addr64_t;

typedef void *asm_func_t;

#if defined(__arm__)
typedef struct {
  uint32_t dummy_0;
  uint32_t dummy_1;

  uint32_t dummy_2;
  uint32_t sp;

  union {
    uint32_t r[13];
    struct {
      uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
    } regs;
  } general;

  uint32_t lr;
} DobbyRegisterContext;
#elif defined(__arm64__) || defined(__aarch64__)
#define ARM64_TMP_REG_NDX_0 17

typedef union _FPReg {
  __int128_t q;
  struct {
    double d1;
    double d2;
  } d;
  struct {
    float f1;
    float f2;
    float f3;
    float f4;
  } f;
} FPReg;

typedef struct {
  uint64_t dmmpy_0;
  uint64_t sp;

  uint64_t dmmpy_1;
  union {
    uint64_t x[29];
    struct {
      uint64_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17, x18, x19, x20, x21,
          x22, x23, x24, x25, x26, x27, x28;
    } regs;
  } general;

  uint64_t fp;
  uint64_t lr;

  union {
    FPReg q[32];
    struct {
      FPReg q0, q1, q2, q3, q4, q5, q6, q7;
      FPReg q8, q9, q10, q11, q12, q13, q14, q15, q16, q17, q18, q19, q20, q21, q22, q23, q24, q25, q26, q27, q28,
          q29, q30, q31;
    } regs;
  } floating;
} DobbyRegisterContext;
#elif defined(_M_IX86) || defined(__i386__)
typedef struct _RegisterContext {
  uint32_t dummy_0;
  uint32_t esp;

  uint32_t dummy_1;
  uint32_t flags;

  union {
    struct {
      uint32_t eax, ebx, ecx, edx, ebp, esp, edi, esi;
    } regs;
  } general;

} DobbyRegisterContext;
#elif defined(_M_X64) || defined(__x86_64__)
typedef struct {
  union {
    struct {
      uint64_t rax, rbx, rcx, rdx, rbp, rsp, rdi, rsi, r8, r9, r10, r11, r12, r13, r14, r15;
    } regs;
  } general;

  uint64_t dummy_0;
  uint64_t flags;
  uint64_t ret;
} DobbyRegisterContext;
#endif

#define install_hook_name(name, fn_ret_t, fn_args_t...)                                                                \
  static fn_ret_t fake_##name(fn_args_t);                                                                              \
  static fn_ret_t (*orig_##name)(fn_args_t);                                                                           \
  static void install_hook_##name(void *sym_addr) {                                                                    \
    DobbyHook(sym_addr, (void *)fake_##name, (void **)&orig_##name);                                                   \
    return;                                                                                                            \
  }                                                                                                                    \
  fn_ret_t fake_##name(fn_args_t)

int DobbyCodePatch(void *address, uint8_t *buffer, uint32_t buffer_size);
typedef struct DobbyHookHandle DobbyHookHandle;
int DobbyHook(void *address, void *fake_func, void **out_origin_func);
int DobbyHookEx(void *address, void *fake_func, void **out_origin_func, DobbyHookHandle **out_handle);
int DobbyUnhook(DobbyHookHandle *handle);
int DobbyDestroyHandle(DobbyHookHandle *handle);
void *DobbyHookHandleTarget(DobbyHookHandle *handle);
void *DobbyHookHandleOrigin(DobbyHookHandle *handle);
int DobbyHookHandleIsActive(DobbyHookHandle *handle);
typedef void (*dobby_instrument_callback_t)(void *address, DobbyRegisterContext *ctx);
int DobbyInstrument(void *address, dobby_instrument_callback_t pre_handler);
int DobbyDestroy(void *address);
const char *DobbyGetVersion();
void *DobbySymbolResolver(const char *image_name, const char *symbol_name);

#define DOBBY_SYMBOL_RESOLVER_DEFAULT 0u
#define DOBBY_SYMBOL_RESOLVER_DYNSYM_ONLY (1u << 0)
#define DOBBY_SYMBOL_RESOLVER_SYMTAB_ONLY (1u << 1)
#define DOBBY_SYMBOL_RESOLVER_FORCE_LOAD (1u << 2)
#define DOBBY_SYMBOL_RESOLVER_FULL_PATHNAME (1u << 3)

void *DobbySymbolResolverEx(const char *image_name, const char *symbol_name, uint32_t flags,
                            size_t *symbol_size);
int DobbyHookBySymbol(const char *image_name, const char *symbol_name, void *fake_func, void **out_origin_func);
int DobbyHookBySymbolEx(const char *image_name, const char *symbol_name, void *fake_func,
                        void **out_origin_func, uint32_t flags, size_t *symbol_size);
typedef void (*dobby_hooked_callback_t)(int error_number, const char *image_name, const char *symbol_name,
                                        void *symbol_addr, void *fake_func, void *origin_func, void *arg);
int DobbyHookBySymbolCallback(const char *image_name, const char *symbol_name, void *fake_func,
                              void **out_origin_func, uint32_t flags, dobby_hooked_callback_t hooked,
                              void *hooked_arg);
int DobbyDestroyBySymbol(const char *image_name, const char *symbol_name);

#if defined(__ANDROID__)
#ifndef IO_GITHUB_HEXHACKING_XDL
#define IO_GITHUB_HEXHACKING_XDL
#include <dlfcn.h>
#include <link.h>

typedef struct {
  const char *dli_fname;
  void *dli_fbase;
  const char *dli_sname;
  void *dli_saddr;
  size_t dli_ssize;
  const ElfW(Phdr) *dlpi_phdr;
  size_t dlpi_phnum;
} xdl_info_t;

#define XDL_DEFAULT 0x00
#define XDL_TRY_FORCE_LOAD 0x01
#define XDL_ALWAYS_FORCE_LOAD 0x02
void *xdl_open(const char *filename, int flags);
void *xdl_open2(struct dl_phdr_info *info);
void *xdl_close(void *handle);
void *xdl_sym(void *handle, const char *symbol, size_t *symbol_size);
void *xdl_dsym(void *handle, const char *symbol, size_t *symbol_size);

#define XDL_NON_SYM 0x01
int xdl_addr(void *addr, xdl_info_t *info, void **cache);
int xdl_addr4(void *addr, xdl_info_t *info, void **cache, int flags);
void xdl_addr_clean(void **cache);

#define XDL_FULL_PATHNAME 0x01
int xdl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data, int flags);

#define XDL_DI_DLINFO 1
int xdl_info(void *handle, int request, void *info);
#endif

void *DobbyXdlOpen(const char *filename, int flags);
void *DobbyXdlOpenFromInfo(struct dl_phdr_info *info);
void *DobbyXdlClose(void *handle);
void *DobbyXdlSym(void *handle, const char *symbol, size_t *symbol_size, uint32_t flags);
void *DobbyXdlAddr(void *addr, xdl_info_t *info, void **cache);
int DobbyXdlInfo(void *handle, xdl_info_t *info);
uintptr_t DobbyGetLibraryBase(const char *image_name);
const char *DobbyGetLibraryPath(void *handle);
int DobbyGetAddressInfo(void *addr, xdl_info_t *info);
#endif

#define DOBBY_ANDROID_NAME_MAX 256

typedef enum {
  DOBBY_ANDROID_OK = 0,
  DOBBY_ANDROID_ERR_INVALID_ARGUMENT = -1,
  DOBBY_ANDROID_ERR_LIBRARY_NOT_FOUND = -2,
  DOBBY_ANDROID_ERR_SYMBOL_NOT_FOUND = -3,
  DOBBY_ANDROID_ERR_ALREADY_HOOKED = -4,
  DOBBY_ANDROID_ERR_HOOK_FAILED = -5,
  DOBBY_ANDROID_ERR_UNHOOK_FAILED = -6,
  DOBBY_ANDROID_ERR_BUFFER_TOO_SMALL = -7,
  DOBBY_ANDROID_ERR_BACKEND_UNSUPPORTED = -8,
  DOBBY_ANDROID_ERR_TRANSACTION_ACTIVE = -9,
} DobbyAndroidStatus;

typedef enum {
  DOBBY_HOOK_BACKEND_AUTO = 0,
  DOBBY_HOOK_BACKEND_INLINE = 1,
  DOBBY_HOOK_BACKEND_PLT = 2,
  DOBBY_HOOK_BACKEND_VTABLE = 3,
} DobbyHookBackend;

typedef struct {
  char image_name[DOBBY_ANDROID_NAME_MAX];
  char symbol_name[DOBBY_ANDROID_NAME_MAX];
  uintptr_t offset;
  void *target_addr;
  void *replace_addr;
  void *origin_addr;
  uintptr_t patch_addr;
  size_t patch_size;
  int enabled;
  int status;
  int backend;
  int kind;
} DobbyAndroidHookRecord;

const char *DobbyAndroidStatusName(int code);
const char *DobbyAndroidGetLastError();
uintptr_t DobbyAndroidGetModuleBase(const char *image_name);
void *DobbyAndroidFindSymbol(const char *image_name, const char *symbol_name);
int DobbyAndroidHookBackend(void *target, void *replace, void **origin, DobbyHookBackend backend);
int DobbyAndroidHookSymbolBackend(const char *image_name, const char *symbol_name, void *replace, void **origin,
                                  DobbyHookBackend backend);
int DobbyAndroidHookFunction(void *target, void *replace, void **origin);
int DobbyAndroidHookSymbol(const char *image_name, const char *symbol_name, void *replace, void **origin);
int DobbyAndroidHookOffset(const char *image_name, uintptr_t offset, void *replace, void **origin);
int DobbyAndroidHookPLT(const char *image_name, const char *symbol_name, void *replace, void **origin);
int DobbyAndroidHookVtable(void *object, int vtable_index, void *replace, void **origin);
int DobbyAndroidUnhook(void *target);
int DobbyAndroidIsHooked(void *target);
int DobbyAndroidListHooks(DobbyAndroidHookRecord *records, int max_count);
int DobbyAndroidClearAllHooks();
int DobbyAndroidBeginTransaction();
int DobbyAndroidCommitTransaction();
int DobbyAndroidRollbackTransaction();

int DobbyImportTableReplace(char *image_name, char *symbol_name, void *fake_func, void **orig_func);
void dobby_set_near_trampoline(bool enable);
typedef addr_t (*dobby_alloc_near_code_callback_t)(uint32_t size, addr_t pos, size_t range);
void dobby_register_alloc_near_code_callback(dobby_alloc_near_code_callback_t handler);
void dobby_set_options(bool enable_near_trampoline, dobby_alloc_near_code_callback_t alloc_near_code_callback);


// ================= Runtime Auto Hook API =================
/*
 * Runtime Auto Hook is a delayed hook scheduler for modules/symbols that may
 * not be ready when your library is loaded. This avoids installing hooks too
 * early and repeatedly checks until the target module/symbol/address becomes
 * available, or until timeout_ms expires.
 *
 * Android note:
 *   When built with DOBBY_ANDROID_USE_XDL, Android module discovery and symbol
 *   resolution prefer xDL. Linux and other supported POSIX builds keep the
 *   normal dl_iterate_phdr / dlsym compatible fallback path.
 *
 * Basic delayed symbol hook example:
 *
 *   #include "dobby.h"
 *
 *   static int (*orig_target_func)(int value) = 0;
 *
 *   static int fake_target_func(int value) {
 *     // Call the original when you need the original behavior.
 *     return orig_target_func ? orig_target_func(value) : value;
 *   }
 *
 *   static void on_target_hooked(int status,
 *                                const DobbyAutoHookDescriptor *desc,
 *                                void *resolved_target,
 *                                void *origin_func,
 *                                void *user_data) {
 *     // status == DOBBY_AUTOHOOK_STATUS_INSTALLED means success.
 *     // status < 0 means timeout, failed, or cancelled.
 *     (void)desc;
 *     (void)resolved_target;
 *     (void)origin_func;
 *     (void)user_data;
 *   }
 *
 *   void install_delayed_hook(void) {
 *     DobbyRuntimeOptions runtime = {0};
 *     runtime.flags = DOBBY_AUTOHOOK_WAIT_MODULE |
 *                     DOBBY_AUTOHOOK_WAIT_SYMBOL |
 *                     DOBBY_AUTOHOOK_RETRY |
 *                     DOBBY_AUTOHOOK_DELAY_FIRST;
 *     runtime.retry_interval_ms = 250;   // Check again every 250 ms.
 *     runtime.timeout_ms = 30000;        // Give up after 30 seconds. 0 = never timeout.
 *     runtime.start_delay_ms = 500;      // First attempt happens after 500 ms.
 *     DobbyEnableRuntime(&runtime);
 *
 *     DobbyAutoHookDescriptor hook = {0};
 *     hook.image_name = "libtarget.so";                  // NULL or empty = search all loaded modules.
 *     hook.symbol_name = "target_func";                  // Exported or, on Android xDL builds, dynsym/symtab symbol.
 *     hook.replace_func = (void *)fake_target_func;      // Your replacement function.
 *     hook.origin_func = (void **)&orig_target_func;     // Receives callable original trampoline.
 *     hook.flags = DOBBY_AUTOHOOK_WAIT_MODULE |
 *                  DOBBY_AUTOHOOK_WAIT_SYMBOL |
 *                  DOBBY_AUTOHOOK_RETRY |
 *                  DOBBY_AUTOHOOK_DELAY_FIRST;
 *     hook.retry_interval_ms = 250;      // 0 = use runtime default.
 *     hook.timeout_ms = 30000;           // 0 = use runtime default; runtime default 0 = no timeout.
 *     hook.start_delay_ms = 1000;        // Delay first install attempt by 1 second.
 *     hook.backend = DOBBY_HOOK_BACKEND_AUTO; // AUTO tries inline first, then PLT when applicable.
 *     hook.callback = on_target_hooked;  // Optional. Called once when installed/failed/timeout/cancelled.
 *     hook.user_data = 0;                // Optional pointer returned to callback.
 *
 *     DobbyAutoHook(&hook);
 *   }
 *
 * Short form, when default delay/retry settings are enough:
 *
 *   DobbyAutoHookSymbol("libtarget.so", "target_func",
 *                       (void *)fake_target_func,
 *                       (void **)&orig_target_func);
 *
 * Blocking form, when the caller wants to wait synchronously:
 *
 *   int rc = DobbyWaitAndHook("libtarget.so", "target_func",
 *                             (void *)fake_target_func,
 *                             (void **)&orig_target_func,
 *                             10000); // 10 second timeout
 *
 * Offset form, when the target is image base + offset:
 *
 *   DobbyWaitAndHookOffset("libtarget.so", 0x1234,
 *                          (void *)fake_target_func,
 *                          (void **)&orig_target_func,
 *                          10000);
 *
 * Direct-address form, when you already have a function address:
 *
 *   DobbyAutoHookDescriptor direct = {0};
 *   direct.target_addr = known_function_address;
 *   direct.replace_func = (void *)fake_target_func;
 *   direct.origin_func = (void **)&orig_target_func;
 *   direct.flags = DOBBY_AUTOHOOK_ADDRESS | DOBBY_AUTOHOOK_RETRY;
 *   DobbyAutoHook(&direct);
 *
 * Flag guide:
 *   DOBBY_AUTOHOOK_NONE         Use runtime defaults when passed in descriptor.flags.
 *   DOBBY_AUTOHOOK_WAIT_MODULE  Wait until image_name is loaded before installing.
 *   DOBBY_AUTOHOOK_WAIT_SYMBOL  Wait until symbol_name resolves before installing.
 *   DOBBY_AUTOHOOK_LINKER_EVENT Reserved for future linker-event driven wakeups.
 *   DOBBY_AUTOHOOK_RETRY        Retry instead of failing immediately when target is not ready.
 *   DOBBY_AUTOHOOK_DELAY_FIRST  Apply start_delay_ms before the first install attempt.
 *   DOBBY_AUTOHOOK_USE_PLT      Prefer PLT/GOT replacement for symbol hooks.
 *   DOBBY_AUTOHOOK_ADDRESS      descriptor.target_addr is the hook target.
 *   DOBBY_AUTOHOOK_OFFSET       descriptor.offset is relative to descriptor.image_name base.
 *
 * Parameter guide:
 *   image_name         Target shared object name, e.g. "libtarget.so". NULL/empty means all modules for symbol lookup.
 *   symbol_name        Target function symbol. Required for symbol/PLT hooks.
 *   replace_func       Replacement function. Required.
 *   origin_func        Optional output pointer. If non-NULL, receives original callable function/trampoline.
 *   retry_interval_ms  Delay between attempts. 0 means use runtime default.
 *   timeout_ms         Maximum wait time. 0 means use runtime default; runtime default 0 means no timeout.
 *   start_delay_ms     Delay before first attempt. Only used with DOBBY_AUTOHOOK_DELAY_FIRST.
 *   offset             RVA from image base. Used with DOBBY_AUTOHOOK_OFFSET / DobbyWaitAndHookOffset.
 *   target_addr        Direct target address. Used with DOBBY_AUTOHOOK_ADDRESS.
 *   backend            DOBBY_HOOK_BACKEND_AUTO, INLINE, PLT, or VTABLE where supported.
 *   callback           Optional completion callback. Called once per scheduled hook.
 *   user_data          Optional opaque pointer passed to callback unchanged.
 *
 * Return guide:
 *   Immediate API return 0 means the request was accepted or installed.
 *   Negative DOBBY_AUTOHOOK_STATUS_* values indicate scheduler failure, timeout,
 *   install failure, or cancellation. For Android helper APIs, use
 *   DobbyAndroidStatusName() and DobbyAndroidGetLastError() for diagnostics.
 */

#define DOBBY_AUTOHOOK_DEFAULT_RETRY_MS 250u
#define DOBBY_AUTOHOOK_DEFAULT_START_DELAY_MS 250u

typedef enum {
  DOBBY_AUTOHOOK_NONE = 0,
  DOBBY_AUTOHOOK_WAIT_MODULE = 1 << 0,
  DOBBY_AUTOHOOK_WAIT_SYMBOL = 1 << 1,
  DOBBY_AUTOHOOK_LINKER_EVENT = 1 << 2,
  DOBBY_AUTOHOOK_RETRY = 1 << 3,
  DOBBY_AUTOHOOK_DELAY_FIRST = 1 << 4,
  DOBBY_AUTOHOOK_USE_PLT = 1 << 5,
  DOBBY_AUTOHOOK_ADDRESS = 1 << 6,
  DOBBY_AUTOHOOK_OFFSET = 1 << 7,
} DobbyAutoHookFlags;

typedef enum {
  DOBBY_AUTOHOOK_STATUS_PENDING = 0,
  DOBBY_AUTOHOOK_STATUS_INSTALLED = 1,
  DOBBY_AUTOHOOK_STATUS_TIMEOUT = -2,
  DOBBY_AUTOHOOK_STATUS_FAILED = -3,
  DOBBY_AUTOHOOK_STATUS_CANCELLED = -4,
} DobbyAutoHookStatus;

typedef struct DobbyAutoHookDescriptor DobbyAutoHookDescriptor;
typedef void (*dobby_autohook_callback_t)(int status,
                                          const DobbyAutoHookDescriptor *descriptor,
                                          void *resolved_target,
                                          void *origin_func,
                                          void *user_data);

struct DobbyAutoHookDescriptor {
  const char *image_name;
  const char *symbol_name;
  void *replace_func;
  void **origin_func;
  uint32_t retry_interval_ms;
  uint32_t timeout_ms;
  uint32_t flags;
  uint32_t start_delay_ms;
  uintptr_t offset;
  void *target_addr;
  int backend;
  dobby_autohook_callback_t callback;
  void *user_data;
};

typedef struct {
  const char *image_name;
  uint32_t flags;
  uint32_t retry_interval_ms;
  uint32_t timeout_ms;
  uint32_t start_delay_ms;
} DobbyRuntimeOptions;

int DobbyEnableRuntime(const DobbyRuntimeOptions *options);
int DobbyDisableRuntime(void);
int DobbyAutoHook(const DobbyAutoHookDescriptor *descriptor);
int DobbyAutoHookSymbol(const char *image_name,
                        const char *symbol_name,
                        void *replace_func,
                        void **origin_func);
int DobbyWaitAndHook(const char *image_name,
                     const char *symbol_name,
                     void *replace_func,
                     void **origin_func,
                     uint32_t timeout_ms);
int DobbyWaitAndHookOffset(const char *image_name,
                           uintptr_t offset,
                           void *replace_func,
                           void **origin_func,
                           uint32_t timeout_ms);
int DobbyAutoHookPendingCount(void);

#ifdef __cplusplus
}
#endif

#endif
