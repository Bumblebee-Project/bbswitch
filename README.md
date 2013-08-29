*Migrated from https://github.com/Lekensteyn/acpi-stuff*

About
-----

bbswitch is a kernel module which automatically detects the required ACPI calls
for two kinds of Optimus laptops. It has been verified to work with "real"
Optimus and "legacy" Optimus laptops (at least, that is how I call them). The
machines on which these tests has performed are:

- Clevo B7130 - GT 425M ("real" Optimus, Lekensteyns laptop)
- Dell Vostro 3500 - GT 310M ("legacy" Optimus, Samsagax' laptop)

(note: there is no need to add more supported laptops here as the universal
calls should work for every laptop model supporting either Optimus calls)

It's preferred over manually hacking with the acpi_call module because it can
detect the correct handle preceding _DSM and has some built-in safeguards:

- You're not allowed to disable a card if a driver (nouveau, nvidia) is loaded.
- Before suspend, the card is automatically enabled. When resuming, it's
  disabled again if that was the case before suspending. Hibernation should
  work, but it not tested.

Precautionary measure : 
- On some machines, turning off the card is permanent and the card does not
  reappear on subsequents reboots, which can result into the screen staying
  black all the time, including the BIOS screen.
  If it occurs, first try to shutdown, unplug power cord, remove battery, wait
  30s, then put everything back in and boot. If it's not solved, then the
  solution is to reset the BIOS to factory settings. Before executing bbswitch
  for the first time, it is therefore recommended to take note of the full key
  sequence in the BIOS to do a reset. 

Build
-----

Build the module (kernel headers are required):

    make
Then load it (requires root privileges, i.e. `sudo`):

    make load
If your card is supported, there should be no error. Otherwise, you get a "No
such device" (ENODEV) error. Check your kernel log (dmesg) for more
information.

DKMS support
------------

If you have DKMS installed, you can install bbswitch in such a way that it
survives kernel upgrades. It is recommended to remove older versions of bbswitch
by running `dkms remove -m bbswitch -v OLDVERSION --all` as root. To install
the new version, simply run:

    # make -f Makefile.dkms

To uninstall it, run:

    # make -f Makefile.dkms uninstall

Usage
-----

bbswitch has three commands to check the card status and switching 
(`#` means "run with root privileges, i.e. run it prefixed with `sudo `):

### Get the status:

    # cat /proc/acpi/bbswitch  
    0000:01:00.0 ON

### Turn the card off, respectively on:

    # tee /proc/acpi/bbswitch <<<OFF
    # tee /proc/acpi/bbswitch <<<ON
If the card stays on when trying to disable it, you've probably forgotten to
unload the driver,

    $ dmesg |tail -1
    bbswitch: device 0000:01:00.0 is in use by driver 'nouveau', refusing OFF

Do **not** attempt to load a driver while the card is off or the card won't be
usable until the PCI configuration space has been recovered (for example, after
writing the contents manually or rebooting).

### Module options

The module has some options that control the behavior on loading and unloading:
`load_state` and `unload_state`. Valid values are `-1`, `0` and `1` meaning "do
not change the card state", "turn the card off" and "turn the card on"
respectively. For example, if you want to have `bbswitch` disable the card
immediately when loading the module while enabling the card on unload, load the
module with:

    # modprobe bbswitch load_state=0 unload_state=1

The `unload_state` value can be changed on runtime, the above command yields the
same behavior as:

    # modprobe bbswitch load_state=0
    # echo 1 | tee /sys/module/bbswitch/parameters/unload_state

If not explictly set, the default behavior is not to change the power state of
the discrete video card which equals to `load_state=-1 unload_state=-1`.

The Lenovo T410 and Lenovo T410s laptops need the module option
`skip_optimus_dsm=1`, otherwise it will detect the wrong methods which result in
the card not being disabled.

### Disable card on boot

These options can be useful to disable the card on boot time. Depending on your
distribution, `/etc/modules`, `/etc/modules.conf` or some other file can be used
to load modules on boot time. Adding the below line to the file makes the card
get disabled on boot:

    bbswitch load_state=0

Users of `kmod` should create `/etc/modprobe.d/bbswitch.conf` containing
`options bbswitch load_state=0` to set the default options. To load the module,
systemd users should create `/etc/modules-load.d/bbswitch.conf` containing
`bbswitch`.

You have to update your initial ramdisk (initrd) for the changes propagate to
the boot process. On Debian and Ubuntu, this can performed by running
`update-initramfs -u` as root.

### Enable card on shutdown

Some machines do not like the card being disabled at shutdown.  
Add the next initscript (`/etc/init/bbswitch.conf`) :

    description "Save power by disabling nvidia on Optimus"
    author      "Lekensteyn <lekensteyn@gmail.com>"
    start on    runlevel [2345]
    stop on     runlevel [016]
    pre-start   exec /sbin/modprobe bbswitch load_state=0 unload_state=1
    pre-stop    exec /sbin/rmmod bbswitch 

Reporting bugs
--------------

This module has been integrated in Bumblebee "Tumbleweed". Please report any
issues on this module in the issue tracker and provide the following details:

- The output of `dmesg | grep -C 10 bbswitch:`
- The kernel version `uname -a`
- Your distribution and version (if applicable)
- The version of your Xorg and the driver
- Submit your machine information on https://bugs.launchpad.net/bugs/752542;
  the instructions are listed in the bug description. Summary: install the
  packages containing `dmidecode`, `acpidump` and `iasl` and then run:

        wget http://lekensteyn.nl/files/get-acpi-info.sh
        sh get-acpi-info.sh
- Information about the ACPI handles associated with PCI devices. Since this is
  a kernel module, you'll need kernel headers, gcc and automake. Commands:

        git clone git://github.com/Lekensteyn/acpi-stuff.git --depth 1
        cd acpi-stuff/acpi_dump_info
        make
        sudo make load
        cat /proc/acpi/dump_info

Upload the generated tarball on the above Launchpad URL and provide a link to
the comment containing your report.
