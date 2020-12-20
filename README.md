# openbsd
# Procedure to test Virtio viomb device and driver stats queue functionality on arm64 device

Note: Viomb device is also called as memory ballooning
## Step 1: Download pre-reqs
Download 
1. miniroot68.img
2. QEMU_EFI.fd
from https://0x16h.github.io/OpenBSD_arm64_qemu.html
## Step 2: Create guest VM
In first terminal execute the following command:
Command 1: apt update && install qemu virt-manager
Now create a root.qcow2 file using command 2
Command 2:
vmctl create -s 10g root.qcow2
Create a go.sh file, this file is used to launch qemu on linux
Command 3: vi go.sh
Note: go.sh should contain the following script:

#!/bin/sh
qemu-system-aarch64 -m 2048 -M virt -cpu cortex-a57 -bios QEMU_EFI.fd -device virtio-rng-device -drive file=miniroot68.img,format=raw,id=drive1 -net user -drive file=root.qcow2,if=none,id=drive0,format=qcow2 -device virtio-blk-device,drive=drive0 -nographic -serial tcp::4450,server,telnet,wait -netdev user,id=net0 -device virtio-net-device,netdev=net0

save the file (esc + :wq!)
Give permission to go.sh file 
Command 4: chmod +x go.sh
Execute the file
Command 5: ./go.sh
## Step 3: Boot guest VM
In second terminal 
Command 6: telnet localhost 4450
Other than the below questions just press enter whenever prompted for an answer
(I)nstall, (U)pgrade, (A)utoinstall or (S)hell? I
System hostname? (short form, e.g. 'foo') hema
Setup a user? (enter a lower-case loginname, or 'no') [no] no
DNS domain name? (e.g. 'example.com') [my.domain] local
Password for root account?
Allow root ssh login? (yes, no, prohibit-password) [no] yes
Use (W)hole disk or (E)dit the MBR? [whole] W
Use (A)uto layout, (E)dit auto layout, or create (C)ustom layout? [a] A
HTTP Server? (hostname, list#, 'done' or '?') cdn.openbsd.org

Note: To take a break, we can stop the environment by ctrl+C in the first terminal.
 To again start the project:
In first terminal execute the command 5 (ie., running the go.sh file if you are doing for the first time otherwise you can run godel.sh file)

## Step 4: Optimization
Stop the first terminal by (ctrl+C) and execute the following commands:
Command 7: cp go.sh godel.sh
Replace the text in godel.sh by following:

#!/bin/sh
qemu-system-aarch64 -m 2048 -M virt -cpu cortex-a57 -bios QEMU_EFI.fd -device virtio-rng-device -net user -drive file=root.qcow2,if=none,id=drive0,format=qcow2 -device virtio-blk-device,drive=drive0 -nographic -serial tcp::4450,server,telnet,wait -netdev user,id=net0 -device virtio-net-device,netdev=net0

Command 8:  chmod +x godel.sh
Command 9: ./godel.sh
## Step 5: Attach Viomb device
Now we need to tell Qemu to attach the virtio balloon device in the .sh script so let’s append the below 
script at the end of the godel.sh script after a space.
-device virtio-balloon-pci -net nic,model=rtl8139
For convenience I create a new file and called as godel-balloon.sh and gave the permission usng chmod +x similar to command 4
The content of godel-balloon.sh file is below:

#!/bin/sh
qemu-system-aarch64 -m 2048 -M virt -cpu cortex-a57 -bios QEMU_EFI.fd -device virtio-rng-device -net user -drive file=root.qcow2,if=none,id=drive0,format=qcow2 -device virtio-blk-device,drive=drive0 -nographic -serial tcp::4450,server,telnet,wait -netdev user,id=net0 -device virtio-net-device,netdev=net0 -device virtio-balloon-pci -net nic,model=rtl8139
## Step 6: Analyze logs in guest VM
Run qemu using godel-balloon.sh in the first terminal, forget about go.sh and godel.sh.
In the second terminal execute command 6. While booting give bsd0 since we compiled the our code into bsd0.
Logs related to balloon is attached give in the viomb_attach functions, and stats queue is initiated and empty req is placed by the driver with all the 6 tags are set to 0x0, all these logs will be visible while booting. So now we successfully initialized an empty stats queue in guest openbsd running on qemu which emulate the arm64 hardware.
## Step 7: Debug from Qemu
Let’s verify the logic of obtaining the guest statistics using stats queue from device side.  For this the device has to make a request. 
stats_poll interval needs to be more than zero to put a request in stats queue hence this need to be set. Here we are doing it manually i.e., giving a hard coded value to test.

Code: on linux, clone qemu from https://github.com/Hemaprasanthi/qemu/commits/master modify the file hw/virtio/virtio-balloon.c in location static void virtio_balloon_device_realize(DeviceState *dev, Error **errp)
add the following code at the  end of the function.
    s->stats_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, balloon_stats_poll_cb, s);
    s->stats_poll_interval = 10;
    /* XXX */
    reset_stats(s);

Before compiling, we need add a target to create qemu-system-x86 object, because qemu emulated the arm64 hardware using that object. By default it creates x86.
Command 10: cd qemu (this qemu is not the one inside openbsd folder)
if it is first time run commands from 11 to 18 else skip
Command 11: git submodule init
Command 12: sudo git submodule update –recursive
Command 13: git submodule status –recursive
Command 14: sudo apt install make -- if make is not installed before
Command 15: sudo make clean
Command 16: mkdir build
Command 17: cd build
Command 18: sudo apt-get update
1.	(https://github.com/Cisco-Talos/pyrebox/issues/41)
sudo apt-get install build-essential zlib1g-dev pkg-config libglib2.0-dev binutils-dev libboost-all-dev autoconf libtool libssl-dev libpixman-1-dev libpython-dev python-pip python-capstone virtualenv libsdl2-dev libusbredirhost-dev flex bison libspice-server-dev libspice-protocol-dev
Command  19: 
../configure  --enable-spice --enable-kvm --enable-usb-redir --target-list=aarch64-softmmu --enable-sdl --enable-debug –extra-cflags="-g3"
Command 20: sudo make config
Command 21: sudo make -j8
Command 22: sudo make install
Command 23: sudo mv /usr/bin/qemu-system-aarch64 /usr/bin/qemu-system-aarch64-old
Command 24: sudo rm /usr/bin/qemu-system-aarch64
Command 25: sudo ln -s /usr/local/bin/qemu-system-aarch64 /usr/bin/qemu-system-aarch64
Command 26: sudo chmod 755 /usr/bin/qemu-system-aarch64
Command 27: ./godel-balloon.sh
Execute command 6 in the second terminal

 
# References:
1. https://github.com/mlarkin2015/openbsd
2. https://github.com/Hemaprasanthi/openbsd
3.	https://github.com/Hemaprasanthi/qemu
4.	https://github.com/Hemaprasanthi/OpenBSD-Deliverables/tree/master/

 
# ERRORS AND SOLUTIONS:
How to resolve RKDRM error?

Error:
o '__asm(".section .rodata,\"a\"");' > gapdummy.c
cc -g -Werror -Wall -Wimplicit-function-declaration  -Wno-uninitialized -Wno-pointer-sign  -Wframe-larger-than=2047 -Wno-address-of-packed-member -Wno-constant-conversion -mcmodel=kernel -mno-red-zone -mno-sse2 -mno-sse -mno-3dnow  -mno-mmx -msoft-float -fno-omit-frame-pointer -ffreestanding -fno-pie -msave-args -O2 -pipe -nostdinc -I/home/openbsd/sys -I/home/openbsd/sys/arch/amd64/compile/GENERIC.MP/obj -I/home/openbsd/sys/arch  -I/home/openbsd/sys/dev/pci/drm/include  -I/home/openbsd/sys/dev/pci/drm/include/uapi  -I/home/openbsd/sys/dev/pci/drm/amd/include/asic_reg  -I/home/openbsd/sys/dev/pci/drm/amd/include  -I/home/openbsd/sys/dev/pci/drm/amd/amdgpu  -I/home/openbsd/sys/dev/pci/drm/amd/display  -I/home/openbsd/sys/dev/pci/drm/amd/display/include  -I/home/openbsd/sys/dev/pci/drm/amd/display/dc  -I/home/openbsd/sys/dev/pci/drm/amd/display/amdgpu_dm  -I/home/openbsd/sys/dev/pci/drm/amd/powerplay/inc  -I/home/openbsd/sys/dev/pci/drm/amd/powerplay/smumgr  -I/home/openbsd/sys/dev/pci/drm/amd/powerplay/hwmgr  -I/home/openbsd/sys/dev/pci/drm/amd/display/dc/inc  -I/home/openbsd/sys/dev/pci/drm/amd/display/dc/inc/hw  -I/home/openbsd/sys/dev/pci/drm/amd/display/dc/clk_mgr  -I/home/openbsd/sys/dev/pci/drm/amd/display/modules/inc  -I/home/openbsd/sys/dev/pci/drm/amd/display/modules/hdcp  -I/home/openbsd/sys/dev/pci/drm/amd/display/dmub/inc  -I/home/openbsd/sys/dev/pci/drm/i915 -DDDB -DDIAGNOSTIC -DKTRACE -DACCOUNTING -DKMEMSTATS -DPTRACE -DPOOL_DEBUG -DCRYPTO -DSYSVMSG -DSYSVSEM -DSYSVSHM -DUVM_SWAP_ENCRYPT -DFFS -DFFS2 -DFFS_SOFTUPDATES -DUFS_DIRHASH -DQUOTA -DEXT2FS -DMFS -DNFSCLIENT -DNFSSERVER -DCD9660 -DUDF -DMSDOSFS -DFIFO -DFUSE -DSOCKET_SPLICE -DTCP_ECN -DTCP_SIGNATURE -DINET6 -DIPSEC -DPPP_BSDCOMP -DPPP_DEFLATE -DPIPEX -DMROUTING -DMPLS -DBOOT_CONFIG -DUSER_PCICONF -DAPERTURE -DMTRR -DNTFS -DHIBERNATE -DPCIVERBOSE -DUSBVERBOSE -DWSDISPLAY_COMPAT_USL -DWSDISPLAY_COMPAT_RAWKBD -DWSDISPLAY_DEFAULTSCREENS="6" -DX86EMU -DONEWIREVERBOSE -DMULTIPROCESSOR -DMAXUSERS=80 -D_KERNEL -MD -MP  -c /home/openbsd/sys/conf/swapgeneric.c
cc -c -g -Werror -Wall -Wimplicit-function-declaration  -Wno-uninitialized -Wno-pointer-sign  -Wframe-larger-than=2047 -Wno-address-of-packed-member -Wno-constant-conversion -mcmodel=kernel -mno-red-zone -mno-sse2 -mno-sse -mno-3dnow  -mno-mmx -msoft-float -fno-omit-frame-pointer -ffreestanding -fno-pie -msave-args -O2 -pipe -nostdinc -I/home/openbsd/sys -I/home/openbsd/sys/arch/amd64/compile/GENERIC.MP/obj -I/home/openbsd/sys/arch  -I/home/openbsd/sys/dev/pci/drm/include  -I/home/openbsd/sys/dev/pci/drm/include/uapi  -I/home/openbsd/sys/dev/pci/drm/amd/include/asic_reg  -I/home/openbsd/sys/dev/pci/drm/amd/include  -I/home/openbsd/sys/dev/pci/drm/amd/amdgpu  -I/home/openbsd/sys/dev/pci/drm/amd/display  -I/home/openbsd/sys/dev/pci/drm/amd/display/include  -I/home/openbsd/sys/dev/pci/drm/amd/display/dc  -I/home/openbsd/sys/dev/pci/drm/amd/display/amdgpu_dm  -I/home/openbsd/sys/dev/pci/drm/amd/powerplay/inc  -I/home/openbsd/sys/dev/pci/drm/amd/powerplay/smumgr  -I/home/openbsd/sys/dev/pci/drm/amd/powerplay/hwmgr  -I/home/openbsd/sys/dev/pci/drm/amd/display/dc/inc  -I/home/openbsd/sys/dev/pci/drm/amd/display/dc/inc/hw  -I/home/openbsd/sys/dev/pci/drm/amd/display/dc/clk_mgr  -I/home/openbsd/sys/dev/pci/drm/amd/display/modules/inc  -I/home/openbsd/sys/dev/pci/drm/amd/display/modules/hdcp  -I/home/openbsd/sys/dev/pci/drm/amd/display/dmub/inc  -I/home/openbsd/sys/dev/pci/drm/i915 -DDDB -DDIAGNOSTIC -DKTRACE -DACCOUNTING -DKMEMSTATS -DPTRACE -DPOOL_DEBUG -DCRYPTO -DSYSVMSG -DSYSVSEM -DSYSVSHM -DUVM_SWAP_ENCRYPT -DFFS -DFFS2 -DFFS_SOFTUPDATES -DUFS_DIRHASH -DQUOTA -DEXT2FS -DMFS -DNFSCLIENT -DNFSSERVER -DCD9660 -DUDF -DMSDOSFS -DFIFO -DFUSE -DSOCKET_SPLICE -DTCP_ECN -DTCP_SIGNATURE -DINET6 -DIPSEC -DPPP_BSDCOMP -DPPP_DEFLATE -DPIPEX -DMROUTING -DMPLS -DBOOT_CONFIG -DUSER_PCICONF -DAPERTURE -DMTRR -DNTFS -DHIBERNATE -DPCIVERBOSE -DUSBVERBOSE -DWSDISPLAY_COMPAT_USL -DWSDISPLAY_COMPAT_RAWKBD -DWSDISPLAY_DEFAULTSCREENS="6" -DX86EMU -DONEWIREVERBOSE -DMULTIPROCESSOR -DMAXUSERS=80 -D_KERNEL -MD -MP gapdummy.c -o gapdummy.o
cc: error: argument unused during compilation: '-mno-sse2' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-mno-sse' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-mno-3dnow' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-mno-mmx' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-msoft-float' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-msave-args' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-mno-sse2' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-mno-sse' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-mno-3dnow' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-mno-mmx' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-msoft-float' [-Werror,-Wunused-command-line-argument]
cc: error: argument unused during compilation: '-msave-args' [-Werror,-Wunused-command-line-argument]
*** Error 1 in target 'swapgeneric.o'
*** Error 1 in /home/openbsd/sys/arch/amd64/compile/GENERIC.MP (Makefile:1772 'gapdummy.o')
*** Error 1 (Makefile:1718 'swapgeneric.o')

Solution:
In file openbsd/sys/dev/fdt/files.fdt uncomment the lines:

device rkdrm: drmbase, wsemuldisplaydev, rasops15, rasops16, rasops24, rasops32

 ######### attach rkdrm at fdt
 ######## file dev/fdt/rkdrm.c rkdrm
 ######### file dev/pci/drm/drm_gem_cma_helper.c rkdrm

### Qemu logs as a host emulating arm64 hardware
![Alt text](https://github.com/Hemaprasanthi/openbsd/blob/master/Host(Qemu)%20Logs.png?raw=true "Qemu logs as a host emulating arm64 hardware")
### OpenBSD logs as a guest on Qemu with arm64 emulated hardware
![Alt text](https://github.com/Hemaprasanthi/openbsd/blob/master/Guest%20VM%20(OpenBSD)%20logs.png?raw=true "OpenBSD logs as a guest on Qemu with arm64 emulated hardware")





