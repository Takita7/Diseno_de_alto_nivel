set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]

set project_file \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.xpr"

open_project $project_file

puts ""
puts "======================================================"
puts "CUSTOM GPGPU IP DEFINITIONS"
puts "======================================================"

set custom_ips [get_ipdefs -all -filter {
    VLNV =~ "*riscv_gpgpu*" ||
    NAME =~ "*gpgpu*" ||
    NAME =~ "*memory_pipeline*"
}]

foreach ip $custom_ips {
    puts [get_property VLNV $ip]
}

puts ""
puts "======================================================"
puts "ALL MATCHING HLS IP DEFINITIONS"
puts "======================================================"

foreach ip [get_ipdefs -all] {
    set name [get_property NAME $ip]
    set vlnv [get_property VLNV $ip]

    if {
        [string match -nocase "*gpgpu*" $name] ||
        [string match -nocase "*memory_pipeline*" $name] ||
        [string match -nocase "*gpgpu*" $vlnv] ||
        [string match -nocase "*memory_pipeline*" $vlnv]
    } {
        puts "NAME = $name"
        puts "VLNV = $vlnv"
        puts "------------------------------------------------------"
    }
}

close_project

puts ""
puts "Inspection finished."