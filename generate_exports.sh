#!/bin/bash

function get_symbol_address()
{
	func=$1
	func_addr=$(sudo grep -w $func /proc/kallsyms | cut -d ' ' -f 1)
	echo $func_addr
}


function generate_one()
{
	#generate inode_c_exported.h
	function_to_export="$1"
	template_file="$2"
	target_file="$3"

	cp $template_file $target_file
	for func in $function_to_export
	do
		echo $func
		addr=$(get_symbol_address $func)
		sed -i "s/"$func"_place_holder/0x$addr/g" $target_file
	done
}

#generate unmap_c_exported.h
function_to_export="ptep_clear_flush rmap_walk __mmu_notifier_invalidate_range_start __get_locked_pte"
template_file="unmap_c_exported_template.h"
target_file="unmap_c_exported.h"
generate_one "$function_to_export" "$template_file" "$target_file"


#generate inode_c_exported.h
function_to_export="__set_page_dirty_no_writeback"
template_file="inode_c_exported_template.h"
target_file="inode_c_exported.h"
generate_one "$function_to_export" "$template_file" "$target_file"
