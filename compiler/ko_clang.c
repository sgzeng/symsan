/*
  The code is modified from AFL's LLVM mode and Angora.

   ------------------------------------------------

   Written by Laszlo Szekeres <lszekeres@google.com> and
              Michal Zalewski <lcamtuf@google.com>

   Copyright 2015, 2016 Google Inc. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

 */

#define KO_MAIN

#include "alloc_inl.h"
#include "defs.h"
#include "debug.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *obj_path;       /* Path to runtime libraries         */
static char **cc_params;     /* Parameters passed to the real CC  */
static u32 cc_par_cnt = 1; /* Param count, including argv0      */
static u8 clang_type = CLANG_FAST_TYPE;
static u8 is_cxx = 0;

/* Try to find the runtime libraries. If that fails, abort. */
static void find_obj(const char *argv0) {

  char *slash, *tmp;
  char path[4096];

  if (!realpath(argv0, path)) {
    FATAL("Cannot get real path of the compiler (%s): %s", argv0, strerror(errno));
  }

  slash = strrchr(path, '/');

  if (slash) {
    char *dir;
    *slash = 0;
    dir = ck_strdup(path);
    *slash = '/';

    tmp = alloc_printf("%s/../lib/symsan/libTaintPass.so", dir);
    if (!access(tmp, R_OK)) {
      obj_path = dir;
      ck_free(tmp);
      return;
    }

    ck_free(tmp);
    ck_free(dir);
  }

  FATAL("Unable to find 'libTaintPass.so' at %s", path);
}

static void check_type(char *name) {
  if (!strcmp(name, "ko-clang++")) {
    is_cxx = 1;
  }
}

static u8 check_if_assembler(u32 argc, char **argv) {
  /* Check if a file with an assembler extension ("s" or "S") appears in argv */

  while (--argc) {
    const char *cur = *(++argv);

    const char *ext = strrchr(cur, '.');
    if (ext && (!strcmp(ext + 1, "s") || !strcmp(ext + 1, "S"))) {
      return 1;
    }
  }

  return 0;
}

static void add_runtime() {
  cc_params[cc_par_cnt++] = "-Wl,--whole-archive";
  if (getenv("KO_ADD_AFLGO")) {
  cc_params[cc_par_cnt++] = alloc_printf("%s/../lib/symsan/libAFLGORT.a", obj_path);
  cc_params[cc_par_cnt++] = "-Wl,--whole-archive";
  }
  cc_params[cc_par_cnt++] = alloc_printf("%s/../lib/symsan/libdfsan_rt-x86_64.a", obj_path);
  cc_params[cc_par_cnt++] = "-Wl,--no-whole-archive";
  cc_params[cc_par_cnt++] =
      alloc_printf("-Wl,--dynamic-list=%s/../lib/symsan/libdfsan_rt-x86_64.a.syms", obj_path);

  cc_params[cc_par_cnt++] = alloc_printf("-Wl,-T%s/../lib/symsan/taint.ld", obj_path);

  if (is_cxx && !getenv("KO_USE_NATIVE_LIBCXX")) {
    cc_params[cc_par_cnt++] = "-Wl,--whole-archive";
    cc_params[cc_par_cnt++] = alloc_printf("%s/../lib/symsan/libc++.a", obj_path);
    cc_params[cc_par_cnt++] = alloc_printf("%s/../lib/symsan/libc++abi.a", obj_path);
    cc_params[cc_par_cnt++] = alloc_printf("%s/../lib/symsan/libunwind.a", obj_path);
    cc_params[cc_par_cnt++] = "-Wl,--no-whole-archive";
    // cc_params[cc_par_cnt++] = alloc_printf("-L%s/../lib/symsan/", obj_path);
  } else {
    cc_params[cc_par_cnt++] = "-lc++";
    cc_params[cc_par_cnt++] = "-lc++abi";
    cc_params[cc_par_cnt++] = "-l:libunwind.so.1";
  }
  cc_params[cc_par_cnt++] = "-lrt";

  cc_params[cc_par_cnt++] = "-Wl,--no-as-needed";
  cc_params[cc_par_cnt++] = "-Wl,--gc-sections"; // if darwin -Wl, -dead_strip
  cc_params[cc_par_cnt++] = "-ldl";
  cc_params[cc_par_cnt++] = "-lpthread";
  cc_params[cc_par_cnt++] = "-lm";

  if (!getenv("KO_NO_NATIVE_ZLIB")) {
    cc_params[cc_par_cnt++] = "-lz";
  }

  if (getenv("KO_USE_Z3")) {
    cc_params[cc_par_cnt++] = "-Wl,--whole-archive";
    cc_params[cc_par_cnt++] = alloc_printf("%s/../lib/symsan/libZ3Solver.a", obj_path);
    cc_params[cc_par_cnt++] = "-Wl,--no-whole-archive";
    cc_params[cc_par_cnt++] = "-lz3";
  }

  if (getenv("KO_USE_FASTGEN")) {
    cc_params[cc_par_cnt++] = "-Wl,--whole-archive";
    cc_params[cc_par_cnt++] = alloc_printf("%s/../lib/symsan/libFastgen.a", obj_path);
    cc_params[cc_par_cnt++] = "-Wl,--no-whole-archive";
  }
}

static void add_aflgo_pass() {
  // if this option is set from the ENV, then appending the following flag to the compiler
  if (getenv("KO_ADD_AFLGO")) {
    cc_params[cc_par_cnt++] = "-Xclang";
    cc_params[cc_par_cnt++] = "-load";
    cc_params[cc_par_cnt++] = "-Xclang";
    cc_params[cc_par_cnt++] = alloc_printf("%s/../lib/symsan/libAFLGOPass.so", obj_path);
    char *target_dir = getenv("AFLGO_TARGET_DIR");
    if (!target_dir)
      FATAL("ENV variable KO_ADD_AFLGO and AFLGO_TARGET_DIR must be set together");
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = alloc_printf("-outdir=%s", target_dir);
    if (getenv("AFLGO_PREPROCESSING")) {
      cc_params[cc_par_cnt++] = "-mllvm";
      cc_params[cc_par_cnt++] = alloc_printf("-targets=%s/BBtargets.txt", target_dir);
    }else{
      cc_params[cc_par_cnt++] = "-mllvm";
      cc_params[cc_par_cnt++] = alloc_printf("-distance=%s/distance.cfg.txt", target_dir);
    }
  }
}

static void add_taint_pass() {
  cc_params[cc_par_cnt++] = "-Xclang";
  cc_params[cc_par_cnt++] = "-load";
  cc_params[cc_par_cnt++] = "-Xclang";
  cc_params[cc_par_cnt++] = alloc_printf("%s/../lib/symsan/libTaintPass.so", obj_path);
  cc_params[cc_par_cnt++] = "-mllvm";
  cc_params[cc_par_cnt++] =
      alloc_printf("-taint-abilist=%s/../lib/symsan/dfsan_abilist.txt", obj_path);

  if (!getenv("KO_NO_NATIVE_ZLIB")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] =
        alloc_printf("-taint-abilist=%s/../lib/symsan/zlib_abilist.txt", obj_path);
  }

  if (getenv("KO_TRACE_FP")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-taint-trace-float-pointer";
  }

  if (is_cxx && getenv("KO_USE_NATIVE_LIBCXX")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] =
        alloc_printf("-taint-abilist=%s/../lib/symsan/libc++_abilist.txt", obj_path);
  }
}

static void edit_params(u32 argc, char **argv) {

  u8 fortify_set = 0, asan_set = 0, x_set = 0, maybe_linking = 1, bit_mode = 0;
  u8 maybe_assembler = 0;
  char *name;

  cc_params = ck_alloc((argc + 128) * sizeof(char *));

  name = strrchr(argv[0], '/');
  if (!name)
    name = argv[0];
  else
    name++;
  check_type(name);

  if (is_cxx) {
    char *alt_cxx = getenv("KO_CXX");
    cc_params[0] = alt_cxx ? alt_cxx : "clang++";
  } else {
    char *alt_cc = getenv("KO_CC");
    cc_params[0] = alt_cc ? alt_cc : "clang";
  }

  maybe_assembler = check_if_assembler(argc, argv);

  /* Detect stray -v calls from ./configure scripts. */
  if (argc == 1 && !strcmp(argv[1], "-v"))
    maybe_linking = 0;

  while (--argc) {
    char *cur = *(++argv);
    // FIXME
    if (!strcmp(cur, "-O1") || !strcmp(cur, "-O2") || !strcmp(cur, "-O3")) {
      //continue;
    }
    if (!strcmp(cur, "-m32"))
      bit_mode = 32;
    if (!strcmp(cur, "-m64"))
      bit_mode = 64;

    if (!strcmp(cur, "-x"))
      x_set = 1;

    if (!strcmp(cur, "-c") || !strcmp(cur, "-S") || !strcmp(cur, "-E"))
      maybe_linking = 0;

    if (!strcmp(cur, "-fsanitize=address") || !strcmp(cur, "-fsanitize=memory"))
      continue; // doesn't work together

    if (strstr(cur, "FORTIFY_SOURCE"))
      fortify_set = 1;

    if (!strcmp(cur, "-shared"))
      maybe_linking = 0;

    if (!strcmp(cur, "-Wl,-z,defs") || !strcmp(cur, "-Wl,--no-undefined"))
      continue;

    cc_params[cc_par_cnt++] = cur;
  }

  if (getenv("KO_CONFIG")) {
    cc_params[cc_par_cnt] = NULL;
    return;
  }

  if (!maybe_assembler) {
    if (getenv("AFLGO_PREPROCESSING")) {
      add_aflgo_pass();
    }else{
      add_taint_pass();
      add_aflgo_pass();
    }
  }

  cc_params[cc_par_cnt++] = "-pie";
  cc_params[cc_par_cnt++] = "-fpic";
  cc_params[cc_par_cnt++] = "-Qunused-arguments";
  cc_params[cc_par_cnt++] = "-fno-vectorize";
  cc_params[cc_par_cnt++] = "-fno-slp-vectorize";
#if 0
  cc_params[cc_par_cnt++] = "-mno-mmx";
  cc_params[cc_par_cnt++] = "-mno-sse";
  cc_params[cc_par_cnt++] = "-mno-sse2";
  cc_params[cc_par_cnt++] = "-mno-avx";
  cc_params[cc_par_cnt++] = "-mno-sse3";
  cc_params[cc_par_cnt++] = "-mno-sse4.1";
  cc_params[cc_par_cnt++] = "-mno-sse4.2";
  cc_params[cc_par_cnt++] = "-mno-ssse3";
  cc_params[cc_par_cnt++] = "-mno-avx2";
  cc_params[cc_par_cnt++] = "-mno-avx512f";
  cc_params[cc_par_cnt++] = "-mno-avx512bw";
  cc_params[cc_par_cnt++] = "-mno-avx512dq";
  cc_params[cc_par_cnt++] = "-mno-avx512vl";
#endif

  if (getenv("KO_HARDEN")) {
    cc_params[cc_par_cnt++] = "-fstack-protector-all";

    if (!fortify_set)
      cc_params[cc_par_cnt++] = "-D_FORTIFY_SOURCE=2";
  }

  if (!getenv("KO_DONT_OPTIMIZE")) {
    cc_params[cc_par_cnt++] = "-g";
    cc_params[cc_par_cnt++] = "-O3";
    cc_params[cc_par_cnt++] = "-funroll-loops";
  }else{
    cc_params[cc_par_cnt++] = "-g";
    cc_params[cc_par_cnt++] = "-O0";
  }

  if (is_cxx && !getenv("KO_USE_NATIVE_LIBCXX")) {
    // FIXME: or use the same header
    // cc_params[cc_par_cnt++] = alloc_printf("-I%s/../include/c++/v1", obj_path);
    cc_params[cc_par_cnt++] = "-stdlib=libc++";
  }

  if (maybe_linking) {

    if (x_set) {
      cc_params[cc_par_cnt++] = "-x";
      cc_params[cc_par_cnt++] = "none";
    }

    add_runtime();

    switch (bit_mode) {
    case 0:
      break;
    case 32:
      /* if (access(cc_params[cc_par_cnt - 1], R_OK)) */
      FATAL("-m32 is not supported by your compiler");
      break;

    case 64:
      /* if (access(cc_params[cc_par_cnt - 1], R_OK)) */
      // FATAL("-m64 is not supported by your compiler");
      break;
    }
  }

  cc_params[cc_par_cnt] = NULL;
}

/* Main entry point */

int main(int argc, char **argv) {

  if (argc < 2) {

    SAYF("\n"
         "This is a helper application for Kirenenko. It serves as a drop-in "
         "replacement\n"
         "for clang, letting you recompile third-party code with the required "
         "runtime\n"
         "instrumentation. A common use pattern would be one of the "
         "following:\n\n"

         "  CC=%s/ko-clang ./configure\n"
         "  CXX=%s/ko-clang++ ./configure\n\n"

         "You can specify custom next-stage toolchain via KO_CC and KO_CXX."
         "You can set (e.g., export) KO_CONFIG=1 to avoid problems during "
         "configure.\n"
         "Setting\n"
         "KO_HARDEN enables hardening optimizations in the compiled "
         "code.\n\n",
         "xx", "xx");

    exit(1);
  }

  find_obj(argv[0]);
  edit_params(argc, argv);
  for (int i = 0; i < cc_par_cnt; i++) {
    printf("%s ", cc_params[i]);
  }
  printf("\n");
  execvp(cc_params[0], (char **)cc_params);

  FATAL("Oops, failed to execute '%s' - check your PATH", cc_params[0]);

  return 0;
}

