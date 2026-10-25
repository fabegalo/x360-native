"""Expose the actual scalar shader functions to the CPU regression executable."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
functions = []
for signature in ("uint XePWLGammaToLinearCode(", "float XeResolveAveragePWLGamma("):
    first = source.index(signature)
    brace = source.index("{", first)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    functions.append(source[first:end])
Path(sys.argv[2]).write_text("// Extracted from the production shader; do not edit.\n"
                            "using uint = unsigned;\nusing std::min;\n" +
                            "\n".join(functions) + "\n", newline="\n")
