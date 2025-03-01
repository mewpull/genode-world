#
# \brief  Example for running go programs with network (based on scrcpy port)
# \author Alexander Tormasov
# \date   2023-01-16
#

#
# check for non-tap (non-linux) version: curl http://127.0.0.1:5555/sdfsdf
#
# To execute this run script on your Linux host you have to do some
# preparation:
#
# 1) Setup a TAP device:
#    ! export USER=[YOUR_USER_NAME]
#    ! export TAP_DEV=tap0
#    ! sudo ip tuntap add dev $TAP_DEV mode tap user $USER
#    ! sudo ip address flush dev $TAP_DEV
#    ! sudo ip address add 10.0.2.1/24 brd 10.0.2.255 dev $TAP_DEV
#    ! sudo ip link set dev $TAP_DEV addr 02:00:00:ca:fe:01
#    ! sudo ip link set dev $TAP_DEV up
#
# 2) Now, start the test and connect using your favorite HTTP client, e.g.:
#    ! cd build/x86_64
#    ! make run/lwip_lx KERNEL=linux BOARD=linux
#    ! lynx -dump http://10.0.2.55
#
#    do not forget to make chmod a+rw /dev/net/tun
# 3) Clean up your Linux when done testing:
#    ! sudo ip tuntap delete $TAP_DEV mode tap
#

# pipe is mandatory for netpoll which is compiled now into libgo by default
set need_pipe 1

# config section
# any libraries for executable above should be listed here
set modules "ld.lib.so libc.lib.so libm.lib.so stdcxx.lib.so vfs.lib.so posix.lib.so"

set conf_verbose "yes"
set conf_ld_verbose "yes"

# for linux - should be pre-configured via linux tap interface
set lx_ip_addr "10.0.2.55"
set lx_gw_addr "10.0.2.1"

set use_nic_router 1
set ld_nic "yes"
#if {[have_board linux]} { set ld_nic "no" }

set use_nic  1
set use_rtc  1

proc socket_fs_plugin {} {
	global use_lxip
	if { $use_lxip } { return lxip }
	return lwip
}

append_if $need_pipe modules { vfs_pipe.lib.so }

append modules { vfs_} [socket_fs_plugin] {.lib.so }

# exception for lxip - need 2 libs
append_if $use_lxip modules { lxip.lib.so }

# build section
set build_components { 
	core init timer lib/ld
}

append build_components { lib/vfs_} [socket_fs_plugin] {}

lappend build_components "$executable_place/$executable"

append_if $need_pipe build_components { lib/vfs_pipe }

create_boot_directory

set depot_pkgs { }
lappend depot_pkgs [depot_user]/src/[base_src]
lappend depot_pkgs [depot_user]/pkg/[drivers_nic_pkg]
lappend depot_pkgs [depot_user]/src/nic_router
lappend depot_pkgs [depot_user]/src/init

if [have_board linux] {
        lappend_if $use_rtc depot_pkgs [depot_user]/src/linux_rtc_drv
} else {
        if [have_board pc] {
                lappend_if $use_rtc depot_pkgs [depot_user]/src/rtc_drv
        } else  {
                lappend_if $use_rtc depot_pkgs [depot_user]/src/dummy_rtc_drv
        }
}

import_from_depot $depot_pkgs

build $build_components

# assemble config for main

set config {
<config>
	<parent-provides>
		<service name="ROM"/>
		<service name="LOG"/>
		<service name="RM"/>
		<service name="CPU"/>
		<service name="PD"/>
		<service name="IRQ"/>
		<service name="IO_PORT"/>
		<service name="IO_MEM"/>
	</parent-provides>
}

append config {
	<default-route>
		<any-service> <parent/> <any-child/> </any-service>
	</default-route>

	<default caps="100"/>
}

append config {
	<start name="timer">
		<resource name="RAM" quantum="3M"/>
		<provides><service name="Timer"/></provides>
	</start>
}

append_if $use_nic config {
	<start name="nic_drv" ld="} $ld_nic {" caps="1000" managing_system="yes" >
		<resource name="RAM" quantum="20M"/>
		<binary name="init"/>
		<route>
			<service name="ROM" label="config"> <parent label="drivers.config"/> </service>
			<service name="Timer"> <child name="timer"/> </service>
			<service name="Uplink"> <child name="nic_router"/> </service>
			<any-service> <parent/> </any-service>
		</route>
	</start>
}

append_if [expr [have_board linux] && $use_rtc] config {
	<start name="linux_rtc_drv" caps="200" ld="no">}
append_if [expr [have_board pc] && $use_rtc] config {
	<start name="rtc_drv" caps="200">}
append_if [expr ![have_board pc] && ![have_board linux] && $use_rtc] config {
	<start name="dummy_rtc_drv" caps="200">}

append_if $use_rtc  config {
		<resource name="RAM" quantum="6M"/>
		<provides> <service name="Rtc"/> </provides>
	</start>
}

append_if $use_nic_router config {
	<start name="nic_router" caps="400">
		<resource name="RAM" quantum="20M"/>
		<provides>
			<service name="Nic"/>
			<service name="Uplink"/>
		</provides>
		<!-- <config verbose="no"
		        verbose_packets="no"
		        verbose_domain_state="no"
		        verbose_packet_drop="no"> -->
		<config>

			<policy label_prefix="} $executable {" domain="client" />
			<policy label_prefix="nic_drv"  domain="uplink"/>

			<domain name="uplink"}
append_if [expr $use_nic_router && [have_board linux]] config {
				interface="} $lx_ip_addr {/24" gateway="} $lx_gw_addr {"}

append_if $use_nic_router config {>
				<nat domain="client" tcp-ports="16384" udp-ports="16384" icmp-ids="16384"/>
				<tcp-forward port="} $executable_port {" domain="client" to="} $executable_ip {" />
			</domain>

			<domain name="client" interface="} $executable_gateway/$executable_net {"
					gateway="} $executable_gateway {" >

				<dhcp-server ip_first="} $executable_ip {" ip_last="} $executable_ip {">
					<dns-server ip="8.8.8.8"/>
					<dns-server ip="1.1.1.1"/>
				</dhcp-server>

				<tcp dst="0.0.0.0/0"><permit-any domain="uplink" /></tcp>
				<udp dst="0.0.0.0/0"><permit-any domain="uplink" /></udp>
				<icmp dst="0.0.0.0/0" domain="uplink"/>

			</domain>

		</config>
		<route>}

append_if [expr $use_nic_router && $use_nic] config {
			<service name="Nic"> <child name="nic_drv"/> </service>}

append_if $use_nic_router config {
			<any-service> <parent/> <any-child/> </any-service>
		</route>
	</start>}

# main golang executable
append config {
	<start name="} $executable {" caps="} $executable_caps {">
		<resource name="RAM" quantum="} $executable_mem {" />
		<config verbose="} $conf_verbose {"  ld_verbose="} $conf_ld_verbose {">
			<arg value="} $executable {" />
			} $arguments {
			<vfs>
				<dir name="dev">
					<log/> <null/> <inline name="rtc">2020-01-01 00:00</inline>
					<inline name="random">0123456789012345678901234567890123456789</inline>
				</dir>
				<dir name="socket">
					<} [socket_fs_plugin] {}
append config { dhcp="yes"/>
				</dir>
}

# add every library/binaries (mandatory for golang stack unwind)
foreach lib $modules {
	append config "<rom name=\"$lib\" label=\"$lib\"/>"
}

append config {
				<rom name="binary" label="} $executable {"/>
				<dir name="proc">
					<dir name="self">
						<rom name="exe" label="} $executable {"/>
					</dir>
				</dir>
}

append_if $need_pipe config {
				<!-- pipe need for network polling -->
				<dir name="pipe"> <pipe/> </dir>
}

append config {
			</vfs>
				<libc stdin="/dev/null" stdout="/dev/log" stderr="/dev/log"
}

append_if $need_pipe config {
				pipe="/pipe"
}

append_if $use_rtc config {
				rtc="/dev/rtc"
}

append config {
				rng="/dev/random" socket="/socket">
					<mmap local_area_default_size="} $arena_mem_size {"  />
				</libc>
			</config>
}

append config {
			<route>
				<service name="Nic"> <child name="nic_router"/> </service>
				<any-service> <parent/> <any-child/> </any-service>
			</route>
		</start>
	</config>
}

install_config $config

# build boot image section

set boot_modules { }

foreach lib $modules {
	lappend boot_modules "$lib"
}

lappend boot_modules "$executable"

build_boot_image $boot_modules

append qemu_args " -m $mem_size -nographic -smp $num_cpu"
append_qemu_nic_args "hostfwd=tcp::5555-:$executable_port"

run_genode_until forever
