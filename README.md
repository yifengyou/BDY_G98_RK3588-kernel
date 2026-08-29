# BDY_G98_RK3588-kernel

This repository contains the kernel source code for BGY G98/Z98 RK3588, adapted from [ophub/linux-6.18.y](https://github.com/ophub/linux-6.18.y).

For documentation and usage guides, please visit: [yifengyou/BDY_G98_RK3588](https://github.com/yifengyou/BDY_G98_RK3588)

本仓库为 彼度云 G98/Z98 RK3588 内核源代码，基于 [ophub/linux-6.18.y](https://github.com/ophub/linux-6.18.y) 进行适配与修改。

文档及使用指南请参见：[yifengyou/BDY_G98_RK3588](https://github.com/yifengyou/BDY_G98_RK3588)


## 更新日志


* 开源RKDevTool刷机工具：新增基于Golang开发的跨平台RKDevTool Web刷机工具，代码100%开源，为用户提供灵活、可定制的固件烧录方案。
* 轻量级Recovery系统：采用专属recovery defconfig进行最小化编译配置，将Recovery镜像体积精准控制在26MB以内，可稳定运行于出厂标配的32MB SPI Nor Flash存储介质中。
* 双存储介质适配：针对SPI Nor Flash与eMMC在硬件层面的物理引脚冲突问题，分别提供两套独立的配置方案，构建系统自动输出对应的两版固件镜像，用户可根据实际硬件选型按需选择使用。

