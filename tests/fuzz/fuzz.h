/* A fuzz target defines LLVMFuzzerTestOneInput, so libFuzzer can run it
 * where it exists, and sk_fuzz_dict, tokens the in-tree driver splices
 * into inputs. A target checks what must hold for every input and
 * aborts when it does not. */
#ifndef SK_FUZZ_H
#define SK_FUZZ_H

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
extern const char *const sk_fuzz_dict[]; /* NULL-terminated */

#endif
