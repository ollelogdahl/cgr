#!/bin/sh

# generate handin

rm -f handin.zip handin-assets.zip

typst c --root report report/report.typ
zip -r handin.zip makefile README.md src/ vendor/ shaders/ report/report.pdf
zip -r handin-assets.zip scenes/ assets/
zip handin-assets.zip -j ~/bin/glslc
