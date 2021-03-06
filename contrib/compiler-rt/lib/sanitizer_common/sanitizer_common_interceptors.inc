//===-- sanitizer_common_interceptors.inc -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Common function interceptors for tools like AddressSanitizer,
// ThreadSanitizer, MemorySanitizer, etc.
//
// This file should be included into the tool's interceptor file,
// which has to define it's own macros:
//   COMMON_INTERCEPTOR_ENTER
//   COMMON_INTERCEPTOR_READ_RANGE
//   COMMON_INTERCEPTOR_WRITE_RANGE
//   COMMON_INTERCEPTOR_FD_ACQUIRE
//   COMMON_INTERCEPTOR_FD_RELEASE
//   COMMON_INTERCEPTOR_SET_THREAD_NAME
//===----------------------------------------------------------------------===//
#include "interception/interception.h"
#include "sanitizer_platform_interceptors.h"

#include <stdarg.h>

#if SANITIZER_WINDOWS
#define va_copy(dst, src) ((dst) = (src))
#endif // _WIN32

#if SANITIZER_INTERCEPT_STRCASECMP
static inline int CharCaseCmp(unsigned char c1, unsigned char c2) {
  int c1_low = ToLower(c1);
  int c2_low = ToLower(c2);
  return c1_low - c2_low;
}

INTERCEPTOR(int, strcasecmp, const char *s1, const char *s2) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strcasecmp, s1, s2);
  unsigned char c1 = 0, c2 = 0;
  uptr i;
  for (i = 0; ; i++) {
    c1 = (unsigned char)s1[i];
    c2 = (unsigned char)s2[i];
    if (CharCaseCmp(c1, c2) != 0 || c1 == '\0')
      break;
  }
  COMMON_INTERCEPTOR_READ_RANGE(ctx, s1, i + 1);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, s2, i + 1);
  return CharCaseCmp(c1, c2);
}

INTERCEPTOR(int, strncasecmp, const char *s1, const char *s2, SIZE_T n) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strncasecmp, s1, s2, n);
  unsigned char c1 = 0, c2 = 0;
  uptr i;
  for (i = 0; i < n; i++) {
    c1 = (unsigned char)s1[i];
    c2 = (unsigned char)s2[i];
    if (CharCaseCmp(c1, c2) != 0 || c1 == '\0')
      break;
  }
  COMMON_INTERCEPTOR_READ_RANGE(ctx, s1, Min(i + 1, n));
  COMMON_INTERCEPTOR_READ_RANGE(ctx, s2, Min(i + 1, n));
  return CharCaseCmp(c1, c2);
}

#define INIT_STRCASECMP INTERCEPT_FUNCTION(strcasecmp)
#define INIT_STRNCASECMP INTERCEPT_FUNCTION(strncasecmp)
#else
#define INIT_STRCASECMP
#define INIT_STRNCASECMP
#endif

#if SANITIZER_INTERCEPT_FREXP
INTERCEPTOR(double, frexp, double x, int *exp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, frexp, x, exp);
  double res = REAL(frexp)(x, exp);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, exp, sizeof(*exp));
  return res;
}

#define INIT_FREXP INTERCEPT_FUNCTION(frexp);
#else
#define INIT_FREXP
#endif // SANITIZER_INTERCEPT_FREXP

#if SANITIZER_INTERCEPT_FREXPF_FREXPL
INTERCEPTOR(float, frexpf, float x, int *exp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, frexpf, x, exp);
  float res = REAL(frexpf)(x, exp);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, exp, sizeof(*exp));
  return res;
}

INTERCEPTOR(long double, frexpl, long double x, int *exp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, frexpl, x, exp);
  long double res = REAL(frexpl)(x, exp);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, exp, sizeof(*exp));
  return res;
}

#define INIT_FREXPF_FREXPL                       \
  INTERCEPT_FUNCTION(frexpf);                    \
  INTERCEPT_FUNCTION(frexpl)
#else
#define INIT_FREXPF_FREXPL
#endif // SANITIZER_INTERCEPT_FREXPF_FREXPL

#if SANITIZER_INTERCEPT_READ
INTERCEPTOR(SSIZE_T, read, int fd, void *ptr, SIZE_T count) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, read, fd, ptr, count);
  SSIZE_T res = REAL(read)(fd, ptr, count);
  if (res > 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, res);
  if (res >= 0 && fd >= 0)
    COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  return res;
}
#define INIT_READ INTERCEPT_FUNCTION(read)
#else
#define INIT_READ
#endif

#if SANITIZER_INTERCEPT_PREAD
INTERCEPTOR(SSIZE_T, pread, int fd, void *ptr, SIZE_T count, OFF_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pread, fd, ptr, count, offset);
  SSIZE_T res = REAL(pread)(fd, ptr, count, offset);
  if (res > 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, res);
  if (res >= 0 && fd >= 0)
    COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  return res;
}
#define INIT_PREAD INTERCEPT_FUNCTION(pread)
#else
#define INIT_PREAD
#endif

#if SANITIZER_INTERCEPT_PREAD64
INTERCEPTOR(SSIZE_T, pread64, int fd, void *ptr, SIZE_T count, OFF64_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pread64, fd, ptr, count, offset);
  SSIZE_T res = REAL(pread64)(fd, ptr, count, offset);
  if (res > 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, res);
  if (res >= 0 && fd >= 0)
    COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd);
  return res;
}
#define INIT_PREAD64 INTERCEPT_FUNCTION(pread64)
#else
#define INIT_PREAD64
#endif

#if SANITIZER_INTERCEPT_WRITE
INTERCEPTOR(SSIZE_T, write, int fd, void *ptr, SIZE_T count) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, write, fd, ptr, count);
  if (fd >= 0)
    COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  SSIZE_T res = REAL(write)(fd, ptr, count);
  if (res > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, res);
  return res;
}
#define INIT_WRITE INTERCEPT_FUNCTION(write)
#else
#define INIT_WRITE
#endif

#if SANITIZER_INTERCEPT_PWRITE
INTERCEPTOR(SSIZE_T, pwrite, int fd, void *ptr, SIZE_T count, OFF_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pwrite, fd, ptr, count, offset);
  if (fd >= 0)
    COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  SSIZE_T res = REAL(pwrite)(fd, ptr, count, offset);
  if (res > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, res);
  return res;
}
#define INIT_PWRITE INTERCEPT_FUNCTION(pwrite)
#else
#define INIT_PWRITE
#endif

#if SANITIZER_INTERCEPT_PWRITE64
INTERCEPTOR(SSIZE_T, pwrite64, int fd, void *ptr, OFF64_T count,
            OFF64_T offset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pwrite64, fd, ptr, count, offset);
  if (fd >= 0)
    COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd);
  SSIZE_T res = REAL(pwrite64)(fd, ptr, count, offset);
  if (res > 0)
    COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, res);
  return res;
}
#define INIT_PWRITE64 INTERCEPT_FUNCTION(pwrite64)
#else
#define INIT_PWRITE64
#endif

#if SANITIZER_INTERCEPT_PRCTL
INTERCEPTOR(int, prctl, int option,
            unsigned long arg2, unsigned long arg3,   // NOLINT
            unsigned long arg4, unsigned long arg5) { // NOLINT
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, prctl, option, arg2, arg3, arg4, arg5);
  static const int PR_SET_NAME = 15;
  int res = REAL(prctl(option, arg2, arg3, arg4, arg5));
  if (option == PR_SET_NAME) {
    char buff[16];
    internal_strncpy(buff, (char *)arg2, 15);
    buff[15] = 0;
    COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, buff);
  }
  return res;
}
#define INIT_PRCTL INTERCEPT_FUNCTION(prctl)
#else
#define INIT_PRCTL
#endif // SANITIZER_INTERCEPT_PRCTL


#if SANITIZER_INTERCEPT_TIME
INTERCEPTOR(unsigned long, time, unsigned long *t) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, time, t);
  unsigned long res = REAL(time)(t);
  if (t && res != (unsigned long)-1) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, t, sizeof(*t));
  }
  return res;
}
#define INIT_TIME                                \
  INTERCEPT_FUNCTION(time);
#else
#define INIT_TIME
#endif // SANITIZER_INTERCEPT_TIME


#if SANITIZER_INTERCEPT_LOCALTIME_AND_FRIENDS
INTERCEPTOR(void *, localtime, unsigned long *timep) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, localtime, timep);
  void *res = REAL(localtime)(timep);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, struct_tm_sz);
  }
  return res;
}
INTERCEPTOR(void *, localtime_r, unsigned long *timep, void *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, localtime_r, timep, result);
  void *res = REAL(localtime_r)(timep, result);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, struct_tm_sz);
  }
  return res;
}
INTERCEPTOR(void *, gmtime, unsigned long *timep) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gmtime, timep);
  void *res = REAL(gmtime)(timep);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, struct_tm_sz);
  }
  return res;
}
INTERCEPTOR(void *, gmtime_r, unsigned long *timep, void *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, gmtime_r, timep, result);
  void *res = REAL(gmtime_r)(timep, result);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, struct_tm_sz);
  }
  return res;
}
INTERCEPTOR(char *, ctime, unsigned long *timep) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ctime, timep);
  char *res = REAL(ctime)(timep);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  }
  return res;
}
INTERCEPTOR(char *, ctime_r, unsigned long *timep, char *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, ctime_r, timep, result);
  char *res = REAL(ctime_r)(timep, result);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, timep, sizeof(*timep));
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  }
  return res;
}
INTERCEPTOR(char *, asctime, void *tm) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, asctime, tm);
  char *res = REAL(asctime)(tm);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, tm, struct_tm_sz);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  }
  return res;
}
INTERCEPTOR(char *, asctime_r, void *tm, char *result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, asctime_r, tm, result);
  char *res = REAL(asctime_r)(tm, result);
  if (res) {
    COMMON_INTERCEPTOR_READ_RANGE(ctx, tm, struct_tm_sz);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  }
  return res;
}
#define INIT_LOCALTIME_AND_FRIENDS               \
  INTERCEPT_FUNCTION(localtime);                 \
  INTERCEPT_FUNCTION(localtime_r);               \
  INTERCEPT_FUNCTION(gmtime);                    \
  INTERCEPT_FUNCTION(gmtime_r);                  \
  INTERCEPT_FUNCTION(ctime);                     \
  INTERCEPT_FUNCTION(ctime_r);                   \
  INTERCEPT_FUNCTION(asctime);                   \
  INTERCEPT_FUNCTION(asctime_r);
#else
#define INIT_LOCALTIME_AND_FRIENDS
#endif // SANITIZER_INTERCEPT_LOCALTIME_AND_FRIENDS

#if SANITIZER_INTERCEPT_SCANF

#include "sanitizer_common_interceptors_scanf.inc"

#define VSCANF_INTERCEPTOR_IMPL(vname, allowGnuMalloc, ...)                    \
  {                                                                            \
    void *ctx;                                                                 \
    COMMON_INTERCEPTOR_ENTER(ctx, vname, __VA_ARGS__);                         \
    va_list aq;                                                                \
    va_copy(aq, ap);                                                           \
    int res = REAL(vname)(__VA_ARGS__);                                        \
    if (res > 0)                                                               \
      scanf_common(ctx, res, allowGnuMalloc, format, aq);                      \
    va_end(aq);                                                                \
    return res;                                                                \
  }

INTERCEPTOR(int, vscanf, const char *format, va_list ap)
VSCANF_INTERCEPTOR_IMPL(vscanf, true, format, ap)

INTERCEPTOR(int, vsscanf, const char *str, const char *format, va_list ap)
VSCANF_INTERCEPTOR_IMPL(vsscanf, true, str, format, ap)

INTERCEPTOR(int, vfscanf, void *stream, const char *format, va_list ap)
VSCANF_INTERCEPTOR_IMPL(vfscanf, true, stream, format, ap)

#if SANITIZER_INTERCEPT_ISOC99_SCANF
INTERCEPTOR(int, __isoc99_vscanf, const char *format, va_list ap)
VSCANF_INTERCEPTOR_IMPL(__isoc99_vscanf, false, format, ap)

INTERCEPTOR(int, __isoc99_vsscanf, const char *str, const char *format,
            va_list ap)
VSCANF_INTERCEPTOR_IMPL(__isoc99_vsscanf, false, str, format, ap)

INTERCEPTOR(int, __isoc99_vfscanf, void *stream, const char *format, va_list ap)
VSCANF_INTERCEPTOR_IMPL(__isoc99_vfscanf, false, stream, format, ap)
#endif  // SANITIZER_INTERCEPT_ISOC99_SCANF

#define SCANF_INTERCEPTOR_IMPL(name, vname, ...)                               \
  {                                                                            \
    void *ctx;                                                                 \
    COMMON_INTERCEPTOR_ENTER(ctx, name, __VA_ARGS__);                          \
    va_list ap;                                                                \
    va_start(ap, format);                                                      \
    int res = vname(__VA_ARGS__, ap);                                          \
    va_end(ap);                                                                \
    return res;                                                                \
  }

INTERCEPTOR(int, scanf, const char *format, ...)
SCANF_INTERCEPTOR_IMPL(scanf, vscanf, format)

INTERCEPTOR(int, fscanf, void *stream, const char *format, ...)
SCANF_INTERCEPTOR_IMPL(fscanf, vfscanf, stream, format)

INTERCEPTOR(int, sscanf, const char *str, const char *format, ...)
SCANF_INTERCEPTOR_IMPL(sscanf, vsscanf, str, format)

#if SANITIZER_INTERCEPT_ISOC99_SCANF
INTERCEPTOR(int, __isoc99_scanf, const char *format, ...)
SCANF_INTERCEPTOR_IMPL(__isoc99_scanf, __isoc99_vscanf, format)

INTERCEPTOR(int, __isoc99_fscanf, void *stream, const char *format, ...)
SCANF_INTERCEPTOR_IMPL(__isoc99_fscanf, __isoc99_vfscanf, stream, format)

INTERCEPTOR(int, __isoc99_sscanf, const char *str, const char *format, ...)
SCANF_INTERCEPTOR_IMPL(__isoc99_sscanf, __isoc99_vsscanf, str, format)
#endif

#define INIT_SCANF                                                             \
  INTERCEPT_FUNCTION(scanf);                                                   \
  INTERCEPT_FUNCTION(sscanf);                                                  \
  INTERCEPT_FUNCTION(fscanf);                                                  \
  INTERCEPT_FUNCTION(vscanf);                                                  \
  INTERCEPT_FUNCTION(vsscanf);                                                 \
  INTERCEPT_FUNCTION(vfscanf);                                                 \
  INTERCEPT_FUNCTION(__isoc99_scanf);                                          \
  INTERCEPT_FUNCTION(__isoc99_sscanf);                                         \
  INTERCEPT_FUNCTION(__isoc99_fscanf);                                         \
  INTERCEPT_FUNCTION(__isoc99_vscanf);                                         \
  INTERCEPT_FUNCTION(__isoc99_vsscanf);                                        \
  INTERCEPT_FUNCTION(__isoc99_vfscanf);

#else
#define INIT_SCANF
#endif

#if SANITIZER_INTERCEPT_GETPWNAM_AND_FRIENDS
INTERCEPTOR(void *, getpwnam, const char *name) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpwnam, name);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  void *res = REAL(getpwnam)(name);
  if (res != 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, struct_passwd_sz);
  return res;
}
INTERCEPTOR(void *, getpwuid, u32 uid) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpwuid, uid);
  void *res = REAL(getpwuid)(uid);
  if (res != 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, struct_passwd_sz);
  return res;
}
INTERCEPTOR(void *, getgrnam, const char *name) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrnam, name);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  void *res = REAL(getgrnam)(name);
  if (res != 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, struct_group_sz);
  return res;
}
INTERCEPTOR(void *, getgrgid, u32 gid) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrgid, gid);
  void *res = REAL(getgrgid)(gid);
  if (res != 0)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, struct_group_sz);
  return res;
}
#define INIT_GETPWNAM_AND_FRIENDS                  \
  INTERCEPT_FUNCTION(getpwnam);                    \
  INTERCEPT_FUNCTION(getpwuid);                    \
  INTERCEPT_FUNCTION(getgrnam);                    \
  INTERCEPT_FUNCTION(getgrgid);
#else
#define INIT_GETPWNAM_AND_FRIENDS
#endif


#if SANITIZER_INTERCEPT_GETPWNAM_R_AND_FRIENDS
INTERCEPTOR(int, getpwnam_r, const char *name, void *pwd,
    char *buf, SIZE_T buflen, void **result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpwnam_r, name, pwd, buf, buflen, result);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  int res = REAL(getpwnam_r)(name, pwd, buf, buflen, result);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pwd, struct_passwd_sz);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  return res;
}
INTERCEPTOR(int, getpwuid_r, u32 uid, void *pwd,
    char *buf, SIZE_T buflen, void **result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getpwuid_r, uid, pwd, buf, buflen, result);
  int res = REAL(getpwuid_r)(uid, pwd, buf, buflen, result);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pwd, struct_passwd_sz);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  return res;
}
INTERCEPTOR(int, getgrnam_r, const char *name, void *grp,
    char *buf, SIZE_T buflen, void **result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrnam_r, name, grp, buf, buflen, result);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, name, REAL(strlen)(name) + 1);
  int res = REAL(getgrnam_r)(name, grp, buf, buflen, result);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, grp, struct_group_sz);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  return res;
}
INTERCEPTOR(int, getgrgid_r, u32 gid, void *grp,
    char *buf, SIZE_T buflen, void **result) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getgrgid_r, gid, grp, buf, buflen, result);
  int res = REAL(getgrgid_r)(gid, grp, buf, buflen, result);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, grp, struct_group_sz);
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, buf, buflen);
  }
  return res;
}
#define INIT_GETPWNAM_R_AND_FRIENDS                \
  INTERCEPT_FUNCTION(getpwnam_r);                  \
  INTERCEPT_FUNCTION(getpwuid_r);                  \
  INTERCEPT_FUNCTION(getgrnam_r);                  \
  INTERCEPT_FUNCTION(getgrgid_r);
#else
#define INIT_GETPWNAM_R_AND_FRIENDS
#endif


#if SANITIZER_INTERCEPT_CLOCK_GETTIME
INTERCEPTOR(int, clock_getres, u32 clk_id, void *tp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, clock_getres, clk_id, tp);
  int res = REAL(clock_getres)(clk_id, tp);
  if (!res && tp) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, tp, struct_timespec_sz);
  }
  return res;
}
INTERCEPTOR(int, clock_gettime, u32 clk_id, void *tp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, clock_gettime, clk_id, tp);
  int res = REAL(clock_gettime)(clk_id, tp);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, tp, struct_timespec_sz);
  }
  return res;
}
INTERCEPTOR(int, clock_settime, u32 clk_id, const void *tp) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, clock_settime, clk_id, tp);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, tp, struct_timespec_sz);
  return REAL(clock_settime)(clk_id, tp);
}
#define INIT_CLOCK_GETTIME                         \
  INTERCEPT_FUNCTION(clock_getres);                \
  INTERCEPT_FUNCTION(clock_gettime);               \
  INTERCEPT_FUNCTION(clock_settime);
#else
#define INIT_CLOCK_GETTIME
#endif


#if SANITIZER_INTERCEPT_GETITIMER
INTERCEPTOR(int, getitimer, int which, void *curr_value) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, getitimer, which, curr_value);
  int res = REAL(getitimer)(which, curr_value);
  if (!res) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, curr_value, struct_itimerval_sz);
  }
  return res;
}
INTERCEPTOR(int, setitimer, int which, const void *new_value, void *old_value) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, setitimer, which, new_value, old_value);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, new_value, struct_itimerval_sz);
  int res = REAL(setitimer)(which, new_value, old_value);
  if (!res && old_value) {
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, old_value, struct_itimerval_sz);
  }
  return res;
}
#define INIT_GETITIMER                             \
  INTERCEPT_FUNCTION(getitimer);                   \
  INTERCEPT_FUNCTION(setitimer);
#else
#define INIT_GETITIMER
#endif


#if SANITIZER_INTERCEPT_GLOB
struct sanitizer_glob_t {
  SIZE_T gl_pathc;
  char **gl_pathv;
};

static void unpoison_glob_t(void *ctx, sanitizer_glob_t *pglob) {
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, pglob, sizeof(*pglob));
  // +1 for NULL pointer at the end.
  COMMON_INTERCEPTOR_WRITE_RANGE(
      ctx, pglob->gl_pathv, (pglob->gl_pathc + 1) * sizeof(*pglob->gl_pathv));
  for (SIZE_T i = 0; i < pglob->gl_pathc; ++i) {
    char *p = pglob->gl_pathv[i];
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, p, REAL(strlen)(p) + 1);
  }
}

INTERCEPTOR(int, glob, const char *pattern, int flags,
            int (*errfunc)(const char *epath, int eerrno),
            sanitizer_glob_t *pglob) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, glob, pattern, flags, errfunc, pglob);
  int res = REAL(glob)(pattern, flags, errfunc, pglob);
  if (res == 0)
    unpoison_glob_t(ctx, pglob);
  return res;
}

INTERCEPTOR(int, glob64, const char *pattern, int flags,
            int (*errfunc)(const char *epath, int eerrno),
            sanitizer_glob_t *pglob) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, glob64, pattern, flags, errfunc, pglob);
  int res = REAL(glob64)(pattern, flags, errfunc, pglob);
  if (res == 0)
    unpoison_glob_t(ctx, pglob);
  return res;
}
#define INIT_GLOB                               \
  INTERCEPT_FUNCTION(glob);                     \
  INTERCEPT_FUNCTION(glob64);
#else // SANITIZER_INTERCEPT_GLOB
#define INIT_GLOB
#endif // SANITIZER_INTERCEPT_GLOB


#if SANITIZER_INTERCEPT_WAIT
INTERCEPTOR(int, wait, int *status) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wait, status);
  int res = REAL(wait)(status);
  if (res != -1 && status)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, status, sizeof(*status));
  return res;
}
INTERCEPTOR(int, waitid, int idtype, int id, void *infop, int options) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, waitid, idtype, id, infop, options);
  int res = REAL(waitid)(idtype, id, infop, options);
  if (res != -1 && infop)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, infop, siginfo_t_sz);
  return res;
}
INTERCEPTOR(int, waitpid, int pid, int *status, int options) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, waitpid, pid, status, options);
  int res = REAL(waitpid)(pid, status, options);
  if (res != -1 && status)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, status, sizeof(*status));
  return res;
}
INTERCEPTOR(int, wait3, int *status, int options, void *rusage) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wait3, status, options, rusage);
  int res = REAL(wait3)(status, options, rusage);
  if (res != -1) {
    if (status)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, status, sizeof(*status));
    if (rusage)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, rusage, struct_rusage_sz);
  }
  return res;
}
INTERCEPTOR(int, wait4, int pid, int *status, int options, void *rusage) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, wait4, pid, status, options, rusage);
  int res = REAL(wait4)(pid, status, options, rusage);
  if (res != -1) {
    if (status)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, status, sizeof(*status));
    if (rusage)
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, rusage, struct_rusage_sz);
  }
  return res;
}
#define INIT_WAIT                                \
  INTERCEPT_FUNCTION(wait);                      \
  INTERCEPT_FUNCTION(waitid);                    \
  INTERCEPT_FUNCTION(waitpid);                   \
  INTERCEPT_FUNCTION(wait3);                     \
  INTERCEPT_FUNCTION(wait4);
#else
#define INIT_WAIT
#endif

#if SANITIZER_INTERCEPT_INET
INTERCEPTOR(char *, inet_ntop, int af, const void *src, char *dst, u32 size) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, inet_ntop, af, src, dst, size);
  uptr sz = __sanitizer_in_addr_sz(af);
  if (sz) COMMON_INTERCEPTOR_READ_RANGE(ctx, src, sz);
  // FIXME: figure out read size based on the address family.
  char *res = REAL(inet_ntop)(af, src, dst, size);
  if (res)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, res, REAL(strlen)(res) + 1);
  return res;
}
INTERCEPTOR(int, inet_pton, int af, const char *src, void *dst) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, inet_pton, af, src, dst);
  // FIXME: figure out read size based on the address family.
  int res = REAL(inet_pton)(af, src, dst);
  if (res == 1) {
    uptr sz = __sanitizer_in_addr_sz(af);
    if (sz) COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, sz);
  }
  return res;
}
#define INIT_INET                                \
  INTERCEPT_FUNCTION(inet_ntop);                 \
  INTERCEPT_FUNCTION(inet_pton);
#else
#define INIT_INET
#endif


#define SANITIZER_COMMON_INTERCEPTORS_INIT                                     \
  INIT_STRCASECMP;                                                             \
  INIT_STRNCASECMP;                                                            \
  INIT_READ;                                                                   \
  INIT_PREAD;                                                                  \
  INIT_PREAD64;                                                                \
  INIT_PRCTL;                                                                  \
  INIT_WRITE;                                                                  \
  INIT_PWRITE;                                                                 \
  INIT_PWRITE64;                                                               \
  INIT_LOCALTIME_AND_FRIENDS;                                                  \
  INIT_SCANF;                                                                  \
  INIT_FREXP;                                                                  \
  INIT_FREXPF_FREXPL;                                                          \
  INIT_GETPWNAM_AND_FRIENDS;                                                   \
  INIT_GETPWNAM_R_AND_FRIENDS;                                                 \
  INIT_CLOCK_GETTIME;                                                          \
  INIT_GETITIMER;                                                              \
  INIT_TIME;                                                                   \
  INIT_GLOB;                                                                   \
  INIT_WAIT;                                                                   \
  INIT_INET;
