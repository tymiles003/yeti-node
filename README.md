# sems-yeti

sems-yeti is a part of project [Yeti]

## Install via Package
```sh
# echo "echo "deb http://pkg.yeti-switch.org/debian wheezy/" > /etc/apt/sources.list.d/yeti.list
# gpg --recv-key 9CEBFFC569A832B6 && gpg -a --export 69A832B6 | apt-key add -
# aptitude update
# aptitude install sems-yeti
```

## Build & Installation (tested on Debian wheezy)

### install build prerequisites
```sh
# aptitude install git cmake build-essential libssl-dev libpqxx3-dev libxml2-dev libspandsp-dev libsamplerate-dev libcurl3-dev libhiredis-dev librtmp-dev libzrtpcpp-dev libev-dev python-dev libspeex-dev libgsm1-dev
```

### get sources & build
```sh
$ git clone https://github.com/yeti-switch/yeti-node
$ cd yeti-node
$ mkdir build && cd build
$ cmake ..
$ make
```

### make debian package
```sh
$ make package
```

### install
```sh
# dpkg -i *.deb
```

### configure
```sh
# cd /etc/sems
# cp sems.conf.dist sems.conf
# cd /etc/sems/etc
# for f in *.dist.conf; do cp -v $f ${f%.dist.conf}.conf; done
```
do not forget to set the correct values in **/etc/sems/etc/yeti.conf**

### run
```sh
# invoke-rc.d sems start
```
[Yeti]:http://yeti-switch.org/
