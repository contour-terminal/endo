#!/usr/bin/env sh

rm -rf ./src/crispy ./src/vtparser ./contour
git clone -n --depth=1 --filter=tree:0 https://github.com/contour-terminal/contour.git
cd contour
git sparse-checkout set --no-cone src/crispy src/vtparser
git checkout
cd ..
mv ./contour/src/crispy ./src/crispy
mv ./contour/src/vtparser ./src/vtparser
rm -rf ./contour
