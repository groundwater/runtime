all: kernel initrd

kernel:
				PATH=/Users/jacob/opt/cross/bin:/Users/jacob/opt/cross/fasm-osx:$$PATH scons

initrd: initrd/*
				./makeinitrd.sh
