# set ddir on command line before exectuing this script:
#    $ ddir="~/xyz" remove_sym_links.sh
[[ "$ddir" == "" ]] && ddir=$HOME/opt
ddir=$(realpath "$ddir")
#echo ddir="$ddir"
rm -f  $ddir/sbin/easycwmp
rm -f  $ddir/sbin/easycwmpd
rm -rf $ddir/usr/share/easycwmp
rm -f  $ddir/etc/config/easycwmp
rm -f  $ddir/lib/functions.sh
rm -f  $ddir/lib/config/uci.sh
rm -f  $ddir/lib/functions/network.sh
rm -f  $ddir/usr/share/libubox
rm -f  $ddir/sbin/uci
