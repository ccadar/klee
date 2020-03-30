/*===-- memcpy.c ----------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===*/

#include <stdlib.h>

void *memcpy(void *destaddr, void const *srcaddr, size_t len) {
  printf("Calling memcpy(%p %p, %u)\n", destaddr, srcaddr, len);
  char *dest = destaddr;
  char const *src = srcaddr;

  while (len-- > 0) {
    char tmp = *src;
    *dest = tmp;
    dest++;
    src++;
  }
  return destaddr;
}
