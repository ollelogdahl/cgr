#!/bin/sh

# generate handin

rm -f handin.zip handin-assets.zip

typst c --root report report/report.typ
zip -r handin.zip makefile README.md src/ vendor/ shaders/ report/report.pdf
zip handin.zip -j ~/bin/glslc
zip -r handin-assets.zip scenes/ assets/