# set sdir and ddir on command line before exectuing this script:
#    $ sdir="~/xyz" create_sym_links.sh
[[ "$sdir" == "" ]] && sdir=.
sdir=$(realpath "$sdir")
[[ "$ddir" == "" ]] && ddir=$HOME/opt
ddir=$(realpath "$ddir")
#echo sdir="$sdir"
#echo ddir="$ddir"
[ ! -d $ddir/usr/share/easycwmp ] && mkdir -p $ddir/usr/share/easycwmp
[ ! -d $ddir/usr/share/easycwmp/functions ] && mkdir -p $ddir/usr/share/easycwmp/functions
ln -sf $sdir/ext/openwrt/scripts/easycwmp.sh                           $ddir/sbin/easycwmp
ln -sf $sdir/bin/easycwmpd                                             $ddir/sbin/easycwmpd
ln -sf $sdir/ext/openwrt/scripts/defaults                              $ddir/usr/share/easycwmp/defaults
ln -sf $sdir/ext/openwrt/scripts/functions/common/common               $ddir/usr/share/easycwmp/functions/common
ln -sf $sdir/ext/openwrt/scripts/functions/common/device_info          $ddir/usr/share/easycwmp/functions/device_info
ln -sf $sdir/ext/openwrt/scripts/functions/common/management_server    $ddir/usr/share/easycwmp/functions/management_server
ln -sf $sdir/ext/openwrt/scripts/functions/common/ipping_launch        $ddir/usr/share/easycwmp/functions/ipping_launch
ln -sf $sdir/ext/openwrt/scripts/functions/tr181/root                  $ddir/usr/share/easycwmp/functions/root
ln -sf $sdir/ext/openwrt/scripts/functions/tr181/ip                    $ddir/usr/share/easycwmp/functions/ip
ln -sf $sdir/ext/openwrt/scripts/functions/tr181/ipping_diagnostic     $ddir/usr/share/easycwmp/functions/ipping_diagnostic
chmod +x $ddir/sbin/easycwmp
chmod +x $sdir/ext/openwrt/scripts/functions/*

# symlink for libubox/jshn.sh
ln -sf $ddir/share/libubox                                             $ddir/usr/share/libubox

# symlink for sbin/uci
ln -sf $ddir/bin/uci                                                   $ddir/sbin/uci

# Make config file
mkdir -p $ddir/etc/config
ln -sf $sdir/ext/openwrt/config/easycwmp                               $ddir/etc/config/easycwmp


# Get some openwrt shell scripts
mkdir -p $ddir/lib/{config,functions}
wget http://pastebin.lukaperkov.net/openwrt/20121219_lib_functions.sh -O $ddir/lib/functions.sh
wget http://pastebin.lukaperkov.net/openwrt/20121219_lib_config_uci.sh -O $ddir/lib/config/uci.sh
wget http://pastebin.lukaperkov.net/openwrt/20121219_lib_functions_network.sh -O $ddir/lib/functions/network.sh

echo Export UCI_CONFIG_DIR and UBUS_SOCKET:
#echo  export UCI_CONFIG_DIR="$sdir/ext/openwrt/config/"
echo  export UCI_CONFIG_DIR="$ddir/etc/config/"
echo  export UBUS_SOCKET="/var/run/ubus.sock"
