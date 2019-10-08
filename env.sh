# set ddir on command line before exectuing this script:
#    $ ddir="~/xyz" remove_sym_links.sh
[[ "$ddir" == "" ]] && ddir=$HOME/opt
ddir=$(realpath "$ddir")
#echo ddir="$ddir"
export PATH=$ddir/sbin:$PATH
export EASYCWMP_INSTALL_DIR=$ddir
export PKG_CONFIG_PATH=$ddir/lib/pkgconfig:/lib/pkgconfig
export LD_LIBRARY_PATH=$ddir/lib
export UCI_CONFIG_DIR="$ddir/etc/config/"
export UBUS_SOCKET="/var/run/ubus.sock"
