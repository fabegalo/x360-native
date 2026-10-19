# Synthetic condition-register aliases; no original executable content.
test_crmove_cross:
  #_ REGISTER_IN r3 0
  crmove 7, 31
  blr
  #_ REGISTER_OUT r3 0
test_crnot_self:
  #_ REGISTER_IN r3 0
  crnot 2, 2
  blr
  #_ REGISTER_OUT r3 0
test_crmove_reverse:
  #_ REGISTER_IN r3 0
  crmove 28, 0
  blr
  #_ REGISTER_OUT r3 0
test_crnot_cross:
  #_ REGISTER_IN r3 0
  crnot 31, 7
  blr
  #_ REGISTER_OUT r3 0
