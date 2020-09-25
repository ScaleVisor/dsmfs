sudo umount /dev/shm/
sudo mount -t dsmfs -osize=6G,mode=1777  tmpfs /dev/shm

