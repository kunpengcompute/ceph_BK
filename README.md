# Ceph
## 介绍
Ceph是一个专注于分布式的、弹性可扩展的、高可靠的、性能优异的存储系统平台，可以同时支持块设备、文件系统和对象网关三种类型的存储接口。
详细特性介绍可参考[Ceph官网](https://ceph.com/en/)

## 仓库说明
EC Turbo是华为自研的Ceph纠删码存储池性能优化特性库，其提升了长度在一个条带内的数据的IO读写性能。本分支主要解决EC Turbo特性在Ceph 14.2.8的适配场景，可优化EC数据存储池性能。

## 支持CPU型号
华为鲲鹏916处理器，华为鲲鹏920处理器

## 支持系统
CentOS 7.6 for aarch64，openEuler 20.03 LTS SP1

## 支持Ceph版本
Ceph 14.2.8 

## 使用说明
使用指南参考文档[EC Turbo-应用加速特性](https://www.hikunpeng.com/document/detail/zh/kunpengsdss/appAccelFeatures/ecturbo/kunpengecturbo_20_0001.html)。


## 参与贡献
如果您想为本仓库贡献代码，请向本仓库任意maintainer发送邮件；
如果您找到产品中的任何Bug，欢迎您提出ISSUE