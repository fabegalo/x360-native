# Synthetic VMX128 dot-product tests. No proprietary instructions/data copied.
# Each function is one instruction + blr (8 bytes): main.cpp calls them by
# address, 0x82010000 + 8 * index, in this order.
test_vmsum3fp128_distinct:
  # Finite inputs whose dot product overflows float32 (rexglue-sdk issue 491).
  #_ REGISTER_IN v3 [FF7FFFFF, FF7FFFFF, FF7FFFFF, 00000000]
  #_ REGISTER_IN v4 [FF7FFFFF, FF7FFFFF, FF7FFFFF, 00000000]
  vmsum3fp128 v5, v3, v4
  blr
  #_ REGISTER_OUT v5 [7FC00000, 7FC00000, 7FC00000, 7FC00000]
test_vmsum3fp128_alias_a:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 01020304]
  #_ REGISTER_IN v4 [40000000, 40700000, 4013d70a, 01020304]
  vmsum3fp128 v3, v3, v4
  blr
  #_ REGISTER_OUT v3 [4122A7F0, 4122A7F0, 4122A7F0, 4122A7F0]
test_vmsum3fp128_alias_b:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 01020304]
  #_ REGISTER_IN v4 [40000000, 40700000, 4013d70a, 01020304]
  vmsum3fp128 v4, v3, v4
  blr
  #_ REGISTER_OUT v4 [4122A7F0, 4122A7F0, 4122A7F0, 4122A7F0]
test_vmsum3fp128_same:
  #_ REGISTER_IN v3 [FF7FFFFF, FF7FFFFF, FF7FFFFF, 00000000]
  vmsum3fp128 v3, v3, v3
  blr
  #_ REGISTER_OUT v3 [7FC00000, 7FC00000, 7FC00000, 7FC00000]
test_vmsum4fp128_distinct:
  #_ REGISTER_IN v3 [FF7FFFFF, FF7FFFFF, FF7FFFFF, FF7FFFFF]
  #_ REGISTER_IN v4 [FF7FFFFF, FF7FFFFF, FF7FFFFF, FF7FFFFF]
  vmsum4fp128 v5, v3, v4
  blr
  #_ REGISTER_OUT v5 [7FC00000, 7FC00000, 7FC00000, 7FC00000]
test_vmsum4fp128_alias_a:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 3ff33333]
  #_ REGISTER_IN v4 [40000000, 40700000, 4013d70a, 40b051eb]
  vmsum4fp128 v3, v3, v4
  blr
  #_ REGISTER_OUT v3 [41A5147B, 41A5147B, 41A5147B, 41A5147B]
test_vmsum4fp128_alias_b:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 3ff33333]
  #_ REGISTER_IN v4 [40000000, 40700000, 4013d70a, 40b051eb]
  vmsum4fp128 v4, v3, v4
  blr
  #_ REGISTER_OUT v4 [41A5147B, 41A5147B, 41A5147B, 41A5147B]
test_vmsum4fp128_same:
  #_ REGISTER_IN v3 [FF7FFFFF, FF7FFFFF, FF7FFFFF, FF7FFFFF]
  vmsum4fp128 v3, v3, v3
  blr
  #_ REGISTER_OUT v3 [7FC00000, 7FC00000, 7FC00000, 7FC00000]

# No directives: compiled, not a test case; kept last (see tests/ppc_smoke).
# --bench: a dependent chain of 6 dot products (no common subexpressions).
# The pipeline's binutils accepts only v0-v7 as destination of these forms.
test_vmsum_bench:
  vmsum3fp128 v0, v3, v4
  vmsum4fp128 v1, v0, v4
  vmsum3fp128 v2, v1, v3
  vmsum4fp128 v5, v2, v4
  vmsum3fp128 v6, v5, v3
  vmsum4fp128 v7, v6, v4
  blr
