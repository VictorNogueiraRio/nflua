git submodule update --init --recursive

make -C deps/lua-memory/src linux CFLAGS="-I/usr/include/lua -fPIC"
rm -f deps/lua-memory/src/*.o

make -C lib/
make -C lua/
make -C iptables/
cp iptables/libxt_*.so /usr/lib/xtables/

make -C /lib/modules/`uname -r`/build M=$PWD CONFIG_LUAUNPACK=m CONFIG_NFLUA=m CONFIG_LUAMEMORY=m NETLINK_NFLUA=31 modules
modprobe nf_conntrack
