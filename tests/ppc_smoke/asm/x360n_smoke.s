# x360-native PowerPC smoke program (original code, written for this project).
#
# Goes through the same pipeline as the SDK's tests/ppc suite:
#   powerpc-none-elf-as/ld (SDK tools/binutils) -> flat .bin at 0x82010000
#   -> rexglue recompile-tests -> C++ -> clang++ -> native Catch2 executable.
# Each test_* label is a function up to the next test_* label
# (src/codegen/test_support.cpp: AnalyzeTestBinary). "#_" lines are the
# REGISTER/MEMORY directives parsed by src/rexglue/commands/test_recompiler.cpp.
#
# Scope: instruction translation, intra-function control flow and resolved
# bl call edges. Not covered: XEX loading, kernel imports, threads, VFS, GPU.

# Counted loop over big-endian words with load-with-update.
# lwz zero-extends, so 0xFFFFFFFF adds as 4294967295: 1+2+3+4+0xFFFFFFFF = 0x1_0000_0009.
test_sum_array:
  #_ MEMORY_IN 10001000 00 00 00 01 00 00 00 02 00 00 00 03 00 00 00 04 ff ff ff ff
  #_ REGISTER_IN r3 0x10001000
  #_ REGISTER_IN r4 5
  li r5, 0
  mtctr r4
  addi r6, r3, -4
sum_loop:
  lwzu r7, 4(r6)
  add r5, r5, r7
  bdnz sum_loop
  blr
  #_ REGISTER_OUT r5 0x0000000100000009
  #_ REGISTER_OUT r6 0x10001010
  #_ REGISTER_OUT r7 0x00000000ffffffff

# Byte-reversed store (the little-endian path guest code uses for GPU data),
# then a normal big-endian load of the same word.
test_store_reversed:
  #_ REGISTER_IN r3 0x11223344
  #_ REGISTER_IN r4 0x10002000
  stwbrx r3, 0, r4
  lwz r5, 0(r4)
  blr
  #_ MEMORY_OUT 10002000 44 33 22 11
  #_ REGISTER_OUT r5 0x44332211

# Two calls to a separate function. LR is kept in a register instead of the
# stack so the test does not depend on a guest stack being set up.
# 7 -> 3*7+1 = 22 -> 3*22+1 = 67.
test_call_helper:
  #_ REGISTER_IN r3 7
  mflr r31
  bl test_helper_triple_plus_one
  bl test_helper_triple_plus_one
  mtlr r31
  blr
  #_ REGISTER_OUT r3 67

# Data-dependent loop with compare/branch, record-form andi. and shifts:
# Collatz sequence from 27 reaches 1 after 111 steps (peak 9232).
# The last andi. runs on 2, so r5 ends at 0.
test_collatz_steps:
  #_ REGISTER_IN r3 27
  li r4, 0
collatz_loop:
  cmplwi r3, 1
  beq collatz_done
  andi. r5, r3, 1
  beq collatz_even
  mulli r3, r3, 3
  addi r3, r3, 1
  b collatz_next
collatz_even:
  srwi r3, r3, 1
collatz_next:
  addi r4, r4, 1
  b collatz_loop
collatz_done:
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 111
  #_ REGISTER_OUT r5 0

# No directives: compiled as a function, not registered as a test case.
# Kept last: ParseTestSpecs consumes "#" lines after a directive-less function
# while looking for outputs, which would swallow a following test label.
test_helper_triple_plus_one:
  mulli r3, r3, 3
  addi r3, r3, 1
  blr
