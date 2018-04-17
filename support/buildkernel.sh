#Steps to build on fedora
export CROSS_COMPILE=arm-linux-gnueabihf-
export ARCH=arm
export MOD_OUT=../_build/modules
export DTBS_OUT=../_build/dtbs
mkdir -p $MOD_OUT
mkdir -p $DTBS_OUT

make sigbox_defconfig
make menuconfig
make savedefconfig
LOADADDR=0x70010000 make uImage
make dtbs
make modules DESTDIR=$MOD_OUT
make modules_install INSTALL_MOD_PATH=$MOD_OUT
