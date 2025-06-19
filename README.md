# Ceph
## 介绍
Ceph是一个专注于分布式的、弹性可扩展的、高可靠的、性能优异的存储系统平台，可以同时支持块设备、文件系统和对象网关三种类型的存储接口。
详细特性介绍可参考[Ceph官网](https://ceph.com/en/)

## 仓库说明
本分支主要解决Ceph对象存储场景下，EC模式带来的元数据放大的问题。Ceph对象存储场景下为了能够提升磁盘利用率，data pool会设置成EC模式来减小数据，元数据的放大倍数为EC副本数，在大比例EC的场景下元数据放大严重，EC元数据缩减特性通过把桶和桶内对象设定为不同的存储类，实现了应用对象数据和元数据分离。这样可以有效地降低元数据的存储开销，从而减少因EC副本数带来的元数据放大效应，提高存储效率。

## 使用说明
使用指南参考文档[EC元数据缩减](https://www.hikunpeng.com/document/detail/zh/kunpengsdss/ecosystemEnable/Ceph/kunpengcephobject_05_0024.html)。

## 参与贡献
如果您想为本仓库贡献代码，请向本仓库任意maintainer发送邮件；
如果您找到产品中的任何Bug，欢迎您提出ISSUE