all: kernel initrd

kernel:
				PATH=/Users/jacob/xcc/bin:/Users/jacob/Projects/NodeOS/Runtime/cross/fasm-osx:$$PATH scons

initrd: initrd/*
				./makeinitrd.sh
