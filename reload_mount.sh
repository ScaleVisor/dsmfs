#!/bin/bash

set -x

sudo umount /dev/shm/
sudo rmmod dsmfs.ko
sudo insmod dsmfs.ko
sudo mount -t dsmfs -osize=6G,mode=1777  tmpfs /dev/shm
