## Welcome the Redis Plugin for Asterisk

Travis Continues Integration Status: [![Travis](http://img.shields.io/travis/dkgroot/res_redis.svg?style=flat)](https://travis-ci.org/dkgroot/res_redis)

This project provides three modules to plugin to asterisk, namely:
- res_redis (Working / Currently Work in progress / Asterisk-11 for now)
  provising distributed devstate between asterisk cluster nodes via redis Pub/Sub
- cdr_redis (To Be Done)
  will provide posting of cdr events to a redis database
- res_config_redis (To be Done)
  will add realtime database capability via redis.

### Prerequisites
- cmake
- libevent
- hiredis >= 0.11

### Configuring
checkout the github repository
    mkdir build
    cd build
    cmake ..

### Build and Install
    make
    make install

- - -
