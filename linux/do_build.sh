curl -L https://github.com/buildroot/buildroot/archive/refs/tags/2023.11.1.tar.gz -o buildroot.tar.gz
tar -xf buildroot.tar.gz && mv buildroot*/ buildroot/
# build linux (this should take a long time)
make -C buildroot BR2_EXTERNAL=$(realpath extern) rv_defconfig 
make -C buildroot 
# build linux once more to fix initrd issues (should not take long)
make -C buildroot linux-rebuild opensbi-rebuild all
