I3C dual-core master/slave test
###############################

Overview
========

This sample demonstrates I3C communication between the two cores of the
LinkedSemi QSH SoC in AMP mode:

- **CPU1 (target core)**: I3C target/slave on ``I3C6``
- **CPU2 (controller core)**: I3C controller/master on ``I3C5``

After initialization, the master writes 2 bytes to the slave, restarts, and
reads 32 bytes back. Both sides print the data they received.

Hardware setup
==============

Connect the I3C5 controller bus to the I3C6 target bus:

- ``PJ10`` (I3C5_SCL) ↔ ``PJ08`` (I3C6_SCL)
- ``PJ11`` (I3C5_SDA) ↔ ``PJ09`` (I3C6_SDA)

Make sure both SCL and SDA have pull-ups. The overlay enables internal
``bias-pull-up2`` on the controller pins, but external pull-ups may still be
required at 12.5 MHz SDR.

Building and Running
====================

Build both images with the provided script::

    ./linkedsemi_zephyr_project/samples/i3c_dualcore_test/build_i3c_dualcore.sh

Or build them manually::

    west build -p always -d build_project/cpu1_i3c_target \
        -b lsqsh_evb@2os/lsqsh/cpu1 \
        ./linkedsemi_zephyr_project/samples/i3c_dualcore_test/cpu1_i3c_target \
        -DDTC_OVERLAY_FILE="app.overlay"

    west build -p always -d build_project/cpu0_i3c_controller \
        -b lsqsh_evb@2os/lsqsh/cpu2 \
        ./linkedsemi_zephyr_project/samples/i3c_dualcore_test/cpu0_i3c_controller \
        -DDTC_OVERLAY_FILE="app.overlay"

Load both images with GDB::

    (gdb) file build_project/cpu1_i3c_target/zephyr/zephyr.elf
    (gdb) load
    (gdb) restore build_project/cpu0_i3c_controller/zephyr/zephyr.bin binary 0x10000000
    (gdb) c

Expected output
===============

CPU1 slave console (``uart1``)::

    I3C slave (CPU1/I3C12) started
    Slave received 2 bytes: AA BB
    Slave received 2 bytes: AA BB
    ...

CPU2 master console (``uart4``)::

    I3C master (CPU2/I3C14) started
    Master wrote: AA BB
    Master read 32 bytes: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F
    ...
