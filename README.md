# mtask
一个Actor并发服务器框架，时钟，组播，网络库，日志系统，监视器，RPC






Build

For linux, install autoconf first for jemalloc(option)

git clone https://github.com/liutianshx2012/mtask.git

cd mtask

make 'PLATFORM'  # PLATFORM can be linux, macosx, freebsd now

Or you can :

export PLAT=linux

make

For freeBSD , use gmake instead of make .

Test

Run these in different console

./mtask examples/config    

./3rd/lua/lua examples/client.lua   # Launch a client, and try to input hello.
