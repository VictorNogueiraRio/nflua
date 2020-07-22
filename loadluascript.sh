LD_LIBRARY_PATH=./lib:./deps/lua-memory/src LUA_CPATH='./lua/?.so;;' \
    XTABLES_LIBDIR=./iptables:/usr/lib/x86_64-linux-gnu/xtables \
    lua nfluactl/nfluactl.lua execute $1 $2
