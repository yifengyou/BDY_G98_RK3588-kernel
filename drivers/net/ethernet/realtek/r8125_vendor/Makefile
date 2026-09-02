# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Realtek RTL8125 2.5GbE PCIe driver
#
# 集成方式：把本文件内容并入
#   drivers/net/ethernet/realtek/Makefile
# （追加在文件末尾即可，不要新建独立目录，除非你打算把源码放到
#   drivers/net/ethernet/realtek/r8125/ 子目录下，那样的话下面这行
#   要改成 obj-$(CONFIG_R8125) += r8125/ 并在子目录里放这份 Makefile）
#

obj-$(CONFIG_R8125) += r8125.o

r8125-objs := r8125_n.o rtl_eeprom.o rtltool.o

r8125-$(CONFIG_R8125_FIBER_SUPPORT)     += r8125_fiber.o
r8125-$(CONFIG_R8125_REALWOW_SUPPORT)   += r8125_realwow.o
r8125-$(CONFIG_R8125_DASH_SUPPORT)      += r8125_dash.o
r8125-$(CONFIG_R8125_PTP_SUPPORT)       += r8125_ptp.o
r8125-$(CONFIG_R8125_RSS_SUPPORT)       += r8125_rss.o
r8125-$(CONFIG_R8125_LIB_SUPPORT)       += r8125_lib.o
r8125-$(CONFIG_R8125_USE_FIRMWARE_FILE) += r8125_firmware.o

# --- 固定开启的宏（原 Makefile 中无对应开关，直接常开）---
ccflags-y += -DCONFIG_R8125_NAPI
ccflags-y += -DCONFIG_R8125_VLAN

# --- 按 Kconfig 选项条件追加的宏 ---
ccflags-$(CONFIG_R8125_SOC_LAN)                    += -DCONFIG_SOC_LAN
ccflags-$(CONFIG_R8125_FIBER_SUPPORT)              += -DENABLE_FIBER_SUPPORT
ccflags-$(CONFIG_R8125_REALWOW_SUPPORT)            += -DENABLE_REALWOW_SUPPORT
ccflags-$(CONFIG_R8125_DASH_SUPPORT)               += -DENABLE_DASH_SUPPORT
ccflags-$(CONFIG_R8125_DOWN_SPEED_100)             += -DCONFIG_DOWN_SPEED_100
ccflags-$(CONFIG_R8125_ASPM)                       += -DCONFIG_ASPM
ccflags-$(CONFIG_R8125_S5WOL)                      += -DENABLE_S5WOL
ccflags-$(CONFIG_R8125_S5_KEEP_CURR_MAC)           += -DENABLE_S5_KEEP_CURR_MAC
ccflags-$(CONFIG_R8125_EEE)                        += -DENABLE_EEE
ccflags-$(CONFIG_R8125_S0_MAGIC_PACKET)            += -DENABLE_S0_MAGIC_PACKET
ccflags-$(CONFIG_R8125_TX_NO_CLOSE)                += -DENABLE_TX_NO_CLOSE
ccflags-$(CONFIG_R8125_MULTIPLE_TX_QUEUE)          += -DENABLE_MULTIPLE_TX_QUEUE
ccflags-$(CONFIG_R8125_PTP_SUPPORT)                += -DENABLE_PTP_SUPPORT
ccflags-$(CONFIG_R8125_RSS_SUPPORT)                += -DENABLE_RSS_SUPPORT
ccflags-$(CONFIG_R8125_LIB_SUPPORT)                += -DENABLE_LIB_SUPPORT
ccflags-$(CONFIG_R8125_USE_FIRMWARE_FILE)          += -DENABLE_USE_FIRMWARE_FILE
ccflags-$(CONFIG_R8125_DISABLE_WOL_SUPPORT)        += -DDISABLE_WOL_SUPPORT
ccflags-$(CONFIG_R8125_DISABLE_MULTI_MSIX_VECTOR)  += -DDISABLE_MULTI_MSIX_VECTOR
ccflags-$(CONFIG_R8125_DOUBLE_VLAN)                += -DENABLE_DOUBLE_VLAN
ccflags-$(CONFIG_R8125_PAGE_REUSE)                 += -DENABLE_PAGE_REUSE
ccflags-$(CONFIG_R8125_RX_PACKET_FRAGMENT)         += -DENABLE_RX_PACKET_FRAGMENT
ccflags-$(CONFIG_R8125_GIGA_LITE)                  += -DENABLE_GIGA_LITE
