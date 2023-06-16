/* This test checks that seeding one file at a time works. 
   It relies on DFS being deterministic on this simple program compiled with no optimisations, across LLVM versions */

// RUN: %clang %s -emit-llvm -g %O0opt -c -o %t.bc
// RUN: rm -rf %t.klee-out

// RUN: %klee --search=dfs --output-dir=%t.klee-out %t.bc
// RUN: test -f %t.klee-out/test000001.ktest
// RUN: test -f %t.klee-out/test000002.ktest
// RUN: test -f %t.klee-out/test000003.ktest
// RUN: test -f %t.klee-out/test000004.ktest
// RUN: not test -f %t.klee-out/test000005.ktest

// RUN: rm -rf %t.klee-out-2
// RUN: %klee --output-dir=%t.klee-out-2 --seed-file %t.klee-out/test000001.ktest --only-replay-seeds %t.bc | FileCheck --check-prefix=SEED_1 %s

// RUN: rm -rf %t.klee-out-2
// RUN: %klee --output-dir=%t.klee-out-2 --seed-file %t.klee-out/test000002.ktest --only-replay-seeds %t.bc | FileCheck --check-prefix=SEED_2 %s

// RUN: rm -rf %t.klee-out-2
// RUN: %klee --output-dir=%t.klee-out-2 --seed-file %t.klee-out/test000003.ktest --only-replay-seeds %t.bc | FileCheck --check-prefix=SEED_3 %s

// RUN: rm -rf %t.klee-out-2
// RUN: %klee --output-dir=%t.klee-out-2 --seed-file %t.klee-out/test000004.ktest --only-replay-seeds %t.bc | FileCheck --check-prefix=SEED_4 %s

#include <stdio.h>

int main(int argc, char **argv) {
  int i;
  long long ll;
  klee_make_symbolic(&i, sizeof(i), "i");
  klee_make_symbolic(&ll, sizeof(ll), "ll");

  if (i == 42)
    printf("i = 42\n");
    // SEED_3: i = 42
    // SEED_4: i = 42
  else
    printf("i != 42\n");
    // SEED_1: i != 42
    // SEED_2: i != 42

  if (ll == 43)
    printf("ll = 43\n");
    // SEED_2: ll = 43
    // SEED_4: ll = 43
  else
    printf("ll != 43\n");
    // SEED_1: ll != 43
    // SEED_3: ll != 43
  return 0;
}
