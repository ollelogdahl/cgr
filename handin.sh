#!/bin/sh

# generate handin

rm -f handin.zip handin-assets.zip

typst c --root docs docs/rend2.typ
zip -r handin.zip makefile README.md src/ vendor/ shaders/ docs/rend2.pdf
zip -r handin-assets.zip scenes/ assets/
zip handin-assets.zip -j ~/bin/glslc
