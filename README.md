## Welcome the Redis Plugin for Asterisk

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
- hiredis

### Configuring
checkout the github repository
    mkdir build
    cd build
    cmake ..

### Build and Install
    make
    make install

- - -
