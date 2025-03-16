[![GitHub Release](https://img.shields.io/github/release/openwrt-xiaomi/kmod-xmir-patcher)](https://github.com/openwrt-xiaomi/kmod-xmir-patcher/releases)
[![Github All Releases](https://img.shields.io/github/downloads/openwrt-xiaomi/kmod-xmir-patcher/total)](https://github.com/openwrt-xiaomi/kmod-xmir-patcher/releases)
[![Github Latest Release](https://img.shields.io/github/downloads/openwrt-xiaomi/kmod-xmir-patcher/latest/total)](https://github.com/openwrt-xiaomi/kmod-xmir-patcher/releases)
[![ViewCount](https://views.whatilearened.today/views/github/openwrt-xiaomi/kmod-xmir-patcher.svg)](https://github.com/openwrt-xiaomi/kmod-xmir-patcher/releases)
[![Hits](https://hits.seeyoufarm.com/api/count/incr/badge.svg?url=https%3A%2F%2Fgithub.com%2Fopenwrt-xiaomi%2Fkmod-xmir-patcher&count_bg=%2379C83D&title_bg=%23555555&icon=&icon_color=%23E7E7E7&title=hits&edge_flat=false)](https://github.com/openwrt-xiaomi/kmod-xmir-patcher/releases)
[![Donations Page](https://github.com/andry81-cache/gh-content-static-cache/raw/master/common/badges/donate/donate.svg)](https://github.com/remittor/donate)

# kmod-XMiR-Patcher
Linux kernel module for hacking xq kernel


## Usage

```
insmod /tmp/xmir_patcher.ko

echo 'get_mtd_info|bdata' > /dev/xmirp
cat /dev/xmirp

echo 'set_mtd_rw|bdata' > /dev/xmirp
cat /dev/xmirp
```

## Donations

[![Donations Page](https://github.com/andry81-cache/gh-content-static-cache/raw/master/common/badges/donate/donate.svg)](https://github.com/remittor/donate)
