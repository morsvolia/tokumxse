#!/bin/sh

if ( test -d build )then
  rm -rf build
fi

mkdir build
cd build

scons -j 4 --opt=on --ssl=on --cc=clang --cxx=clang++ \
	--libc++=on --c++11=on \
	--use-system-pcre=on \
	--use-system-snappy=on \
	--use-system-zlib=on \
	--js-engine=v8-3.25 \
	--libpath=/home/macify/Downloads/tokumxse/tokumxse/src/third_party/tokuft/lib \
	--cpppath=/home/macify/Downloads/tokumxse/tokumxse/src/third_party/tokuft/include \
	--tokuft --allocator=jemalloc \
	-C .. core tools

