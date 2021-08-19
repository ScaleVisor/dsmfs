cmd_fs/dsmfs/modules.order := {   echo fs/dsmfs/dsmfs.ko; :; } | awk '!x[$$0]++' - > fs/dsmfs/modules.order
