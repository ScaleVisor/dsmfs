cmd_fs/dsmfs/dsmfs.o := ld  -m elf_x86_64    -r -o fs/dsmfs/dsmfs.o fs/dsmfs/init.o fs/dsmfs/file-mmu.o fs/dsmfs/inode.o fs/dsmfs/channel.o fs/dsmfs/dsm.o fs/dsmfs/unmap.o
