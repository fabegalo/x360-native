# cmake -DINPUT=<file> -DOUTPUT=<file.c> -DSYMBOL=<name> -P x360n_embed_file.cmake
file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" length)
math(EXPR size "${length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}.tmp" "/* Generated from ${INPUT}; do not edit. */\n"
  "const unsigned char ${SYMBOL}[] = {\n${bytes}0};\n"
  "const unsigned int ${SYMBOL}_size = ${size};\n")
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
