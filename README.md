# Memory Replay PoC for Xen Project*

This project is intended to illustrate the harnessing required to perform
memory replay-based fuzzing through the Xen VMI API. The tool utilizes Xen VM
forks to perform the fuzzing. The tool records memory accesses made by a target
process within a VM and replays the previously recorded values to future memory
accesses.

Code coverage information is collected purely for debugging purposes to verify
when new paths are triggered. Code coverage is implemented using Capstone to
dynamically disassemble the target code to locate the next control-flow
instruction. The instruction is breakpointed and when the breakpoint triggers,
MTF is activated to advance the VM ahead, then the processes is repeated again.

This project is licensed under the terms of the MIT license

# Setup instruction for Ubuntu:

The following instructions have been mainly tested on Ubuntu 20.04. The actual
package names may vary on different distros/versions. You may also find
https://wiki.xenproject.org/wiki/Compiling_Xen_From_Source helpful if you run
into issues.

# 1. Install dependencies
----------------------------------
sudo apt install git build-essential libfdt-dev libpixman-1-dev libssl-dev \
     libsdl1.2-dev autoconf libtool xtightvncviewer tightvncserver x11vnc \
     libsdl1.2-dev uuid-runtime uuid-dev bridge-utils python3-dev liblzma-dev \
     libc6-dev wget git bcc bin86 gawk iproute2 libcurl4-openssl-dev bzip2 \
     libpci-dev libc6-dev libc6-dev-i386 linux-libc-dev zlib1g-dev \
     libncurses5-dev patch libvncserver-dev libssl-dev libsdl-dev iasl \
     libbz2-dev e2fslibs-dev ocaml libx11-dev bison flex ocaml-findlib \
     xz-utils gettext libyajl-dev libpixman-1-dev libaio-dev libfdt-dev \
     cabextract libglib2.0-dev autoconf automake libtool libjson-c-dev \
     libfuse-dev liblzma-dev autoconf-archive kpartx python3-pip gcc-7 \
     libsystemd-dev cmake snap

# 2. Grab the project and all submodules
----------------------------------
git clone git://xenbits.xen.org/people/tklengyel/memory-replay.git
cd memory-replay
git submodule update --init

# 3. Compile & Install Xen
----------------------------------
There had been some compiler issues with newer gcc's so set your gcc version to GCC-7:

sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 7

Make sure the pci include folder exists at `/usr/include/pci`. In case it doesn't create a symbolic link to where it's installed at:

sudo ln -s /usr/include/x86_64-linux-gnu/pci /usr/include/pci

Now we can compile Xen:

cd xen
echo XEN_CONFIG_EXPERT=y > .config
echo CONFIG_MEM_SHARING=y > xen/.config
./configure --disable-pvshim --enable-githttp
make -C xen olddefconfig
make -j4 dist-xen
make -j4 dist-tools
sudo su
make -j4 install-xen
make -j4 install-tools
echo "/usr/local/lib" > /etc/ld.so.conf.d/xen.conf
ldconfig
echo "none /proc/xen xenfs defaults,nofail 0 0" >> /etc/fstab
systemctl enable xen-qemu-dom0-disk-backend.service
systemctl enable xen-init-dom0.service
systemctl enable xenconsoled.service
echo "GRUB_CMDLINE_XEN_DEFAULT=\"console=vga hap_1gb=false hap_2mb=false\""
update-grub
reboot

Make sure to pick the Xen entry in GRUB when booting. You can verify you booted
into Xen correctly by running `xen-detect`.

## 3.b Booting from UEFI

If Xen doesn't boot from GRUB you can try to boot it from UEFI directly:

mkdir -p /boot/efi/EFI/xen
cp /usr/lib/efi/xen.efi /boot/efi/EFI/xen
cp /boot/vmlinuz /boot/efi/EFI/xen
cp /boot/initrd.img /boot/efi/EFI/xen

Gather your kernel boot command line from /proc/cmdline & paste the following
into /boot/efi/EFI/xen/xen.cfg:

    [global]
    default=xen

    [xen]
    options=console=vga hap_1gb=false hap_2mb=false
    kernel=vmlinuz console=hvc0 earlyprintk=xen <KERNEL'S BOOT COMMAND LINE>
    ramdisk=initrd.img

Create an EFI boot entry for it:

efibootmgr -c -d /dev/sda -p 1 -w -L "Xen" -l "\EFI\xen\xen.efi"
reboot

You may want to use the `-C` option above if you are on a remote system so you
can set only the next-boot to try Xen. This is helpful in case the system can't
boot Xen and you don't have remote KVM to avoid losing access in case Xen can't
boot for some reason. Use `efibootmgr --bootnext <BOOT NUMBER FOR XEN>` to try
boot Xen only on the next reboot.

# 4. Create VM disk image
----------------------------------
20GB is usually sufficient but if you are planning to compile the kernel from source you will want to increase that.

dd if=/dev/zero of=vmdisk.img bs=1G count=20 

# 5. Setup networking
----------------------------------
sudo brctl addbr xenbr0
sudo ip addr add 10.0.0.1/24 dev xenbr0
sudo ip link set xenbr0 up
sudo echo 1 > /proc/sys/net/ipv4/ip_forward
sudo iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE

You might also want to save this as a script or add it to /etc/rc.local. See
https://www.linuxbabe.com/linux-server/how-to-enable-etcrc-local-with-systemd
for more details.

# 6. Create VM
----------------------------------
Paste the following as your domain config, for example into `debian.cfg`, tune it as you see fit. It's important the VM has only a single vCPU.

    name="debian"
    builder="hvm"
    vcpus=1
    maxvcpus=1
    memory=2048
    maxmem=2048
    hap=1
    boot="cd"
    serial="pty"
    vif=['bridge=xenbr0']
    vnc=1
    vnclisten="0.0.0.0"
    vncpasswd='1234567'
    usb=1
    usbdevice=['tablet']
    # Make sure to update the paths below!
    disk=['file:/path/to/vmdisk.img,xvda,w',
          'file:/path/to/debian.iso,xvdc:cdrom,r']

Start the VM with:

sudo xl create -V debian.cfg

Follow the installation instructions in the VNC session. Configure the network
manually to 10.0.0.2 with a default route via 10.0.0.1

# 7. Grab the kernel's debug symbols & headers
----------------------------------
On Debian systems you can install everything right away

su -
apt update && sudo apt install linux-image-$(uname -r)-dbg \
    linux-headers-$(uname -r)

On Ubuntu to install the Kernel debug symbols please follow the following
tutorial: https://wiki.ubuntu.com/Debug%20Symbol%20Packages

From the VM copy `/usr/lib/debug/boot/vmlinux-$(uname -r)` and
`/boot/System.map-$(uname -r)` to your dom0, for example using scp.

# 8. Build the kernel's debug JSON profile
---------------------------------
Change the paths to match your setup

sudo snap install --classic go
cd dwarf2json
go build
./dwarf2json linux --elf /path/to/vmlinux --system-map /path/to/System.map \
    > ~/debian.json
cd ..

# 9. Compile & install Capstone
---------------------------------
We use a more recent version from the submodule (4.0.2) then what most distros
ship by default. If your distro ships a newer version you could also just
install `libcapstone-dev`.

cd capstone
mkdir build
cd build
cmake ..
make
make install
cd ../..

# 10. Compile & install LibVMI
---------------------------------
cd libvmi
autoreconf -vif
./configure --disable-kvm --disable-bareflank --disable-file
make -j4
sudo make install
cd ..

Test that base VMI works with:
sudo vmi-process-list --name debian --json ~/debian.json

# 11. Compile shredder
---------------------------------
autoreconf -vif
./configure
make -j4

# 12. Use gdb to add the harness breakpoints to the target process
---------------------------------
You can use a standard debugger to place the start and end harness breakpoints
to your target process. Or you can also add it via inline assembly:

static inline void harness(void)
{
    asm ("int3");
}

You can insert the harness before and after the code segment you want to fuzz:

    harness();
    // code to test here
    harness();

# 13. Start fuzzing 
---------------------------------
Start running shredder:
sudo ./shredder --domain debian --json-path /path/to/kernel.json --fuzz

Execute the target application in the VM to start the fuzzing session.

---------------------------------
*Other names and brands may be claimed as the property of others
