#!/bin/bash

uninstall_bbswitch_modules()
{
    local PKGVER="$(ls /usr/src/ | grep bbswitch | sed 's/^.*-//')"


    # Warning: dmks say error if it doesn't know bbswitch module.
    # This is the case for the first time, but it's not an error!
    # TODO: Don't use the code below if PKGVER is empty (no return,
    # no space...)


    # Remove all version of bbswitch known by dkms
    dkms remove -m bbswitch -v $PKGVER --all
    depmod -a

    # Remove all sources of bbswitch
    # Keep rm after dkms remove because this last use dkms.conf
    rm -fr /usr/src/bbswitch-$PKGVER
}


install_bbswitch_module()
{
    # Before install, remove previous versions
    uninstall_bbswitch_modules

    # Give a version to this bbswitch module
    local GITVERSION=`git describe --tags`

    sudo mkdir -p "/usr/src/bbswitch-$GITVERSION"

    cp *.c "/usr/src/bbswitch-$GITVERSION" 
    cp Makefile "/usr/src/bbswitch-$GITVERSION" 
    cp ./dkms/dkms.conf "/usr/src/bbswitch-$GITVERSION" 

    sed "s/REPLACE/$GITVERSION/" "/usr/src/bbswitch-$GITVERSION/dkms.conf"

    # Compilation and installation
    dkms add -m bbswitch -v $GITVERSION
    dkms build -m bbswitch -v $GITVERSION
    dkms install -m bbswitch -v $GITVERSION
    depmod -a

    #Now user can do : modprobe bbswitch
}


case $1 in
	"install") install_bbswitch_module;;

	"uninstall") uninstall_bbswitch_modules;;

	*)  echo "Error: Don't call this script directly";;
esac

