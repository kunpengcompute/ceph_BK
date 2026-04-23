# README<a name="ZH-CN_TOPIC_0000002552469311"></a>

## 项目介绍<a name="ZH-CN_TOPIC_0000002521315532"></a>

随着5G、AI与云计算业务的飞速发展，数据量呈现爆发式增长。多样化的业务对分布式存储系统提出了更严苛的性能诉求，如何在纠删码（EC）模式下保持高IOPS、如何降低跨节点通信时延、以及如何提升存储空间利用率，成为现代大规模集群面临的核心挑战。

本仓库针对开源Ceph框架进行了特性增强，重点围绕EC Turbo性能优化、SPDK IO加速、数据压紧、以及RDMA网络加速四大维度，构建了一套面向高性能场景的分布式存储技术栈。

## 目录结构

```txt
├── docs                                                    # 项目文档目录
│   ├── LICENSE                                             # 软件许可协议
│   └── zh                                                  # 中文文档目录
│       ├── figures                                         # 中文文档图片资料目录
│       ├── public_sys-resources                            # 中文公共资源目录
│       ├── data_compaction_feature_guide.md                # 数据压紧特性指南
│       ├── ec_turbo_feature_guide.md                       # EC Turbo特性指南
│       ├── ec_turbo_tuning_guide.md                        # EC Turbo调优指南
│       ├── rdma_network_acceleration_feature_guide.md      # RDMA网络加速特性指南
│       └── spdk_io_acceleration_feature_guide.md           # SPDK IO加速特性指南
├── ceph-14.2.8-compaction.patch                            # 数据压紧 Ceph适配补丁
├── ceph-14.2.8-ucx.patch                                   # RDMA网络加速 Ceph适配UCX的补丁
├── ceph-17.2.x-spdk.patch                                  # SPDK IO加速 Ceph适配SPDK的补丁 
├── ceph-17.2.x-ucx.patch                                   # SPDK IO加速 Ceph适配UCX的补丁
├── ceph-14.2.8-ec_turbo-release.patch                      # EC Turbo Ceph适配补丁
├── ceph-ecturbo-optimization.patch                         # EC Turbo Ceph优化补丁
└── README.md                                               # 介绍文档
```

## 特性简介<a name="ZH-CN_TOPIC_0000002521064452"></a>

|特性名称|特性文档|特性介绍|
|--|--|--|
|EC Turbo|[EC Turbo特性指南](docs/zh/ec_turbo_feature_guide.md)|EC Turbo是华为自研的Ceph纠删码存储池性能优化特性库，EC Turbo提升了长度在一个条带内的数据的I/O读写性能。|
|EC Turbo调优|[EC Turbo调优指南](docs/zh/ec_turbo_tuning_guide.md)|Ceph叠加EC Turbo特性后，优化其在EC模式下4k块集群的读写性能。|
|SPDK IO加速|[SPDK IO加速特性指南](docs/zh/spdk_io_acceleration_feature_guide.md)|容器化部署Ceph并配置和优化SPDK、UCX、KSAL的集成，以实现存储与网络性能的最大化，满足现代分布式存储对高吞吐量、低延迟的需求。|
|数据压紧|[数据压紧特性指南](docs/zh/data_compaction_feature_guide.md)|通过消除补零对齐操作带来的数据浪费问题，结合压紧封装、空间计数分配、粒度分流、聚合提交、批量回调等手段提升数据缩减率并提升Ceph系统整体IOPS，实现成本性能双收益。|
|RDMA网络加速|[RDMA网络加速特性指南](docs/zh/rdma_network_acceleration_feature_guide.md)|RDMA网络加速特性主要为使用UCX网络框架使能RDMA网络，并应用于Ceph集群前后端网络。|

## 免责声明<a name="ZH-CN_TOPIC_0000002551989693"></a>

**致本项目使用者**

- 本项目仅供调试和开发之用，使用者需自行承担使用风险，并理解以下内容：
    - 数据处理及删除：用户在使用本工具过程中产生的数据属于用户责任范畴。建议用户在使用完毕后及时删除相关数据，以防信息泄露。
    - 数据保密与传播：使用者了解并同意不得将通过本工具产生的数据随意外发或传播。对于由此产生的信息泄露、数据泄露或其他不良后果，本工具及其开发者概不负责。
    - 用户输入安全性：用户需自行保证输入的命令行的安全性，并承担因输入不当而导致的任何安全风险或损失。对于输入命令行不当所导致的问题，本工具及其开发者概不负责。

- 免责声明范围：本免责声明适用于所有使用本工具的个人或实体。使用本工具即表示您同意并接受本声明的内容，并愿意承担因使用该功能而产生的风险和责任，如有异议请停止使用本工具。
- 在使用本工具之前，请**谨慎阅读并理解以上免责声明的内容**。对于使用本工具所产生的任何问题或疑问，请及时联系开发者。

**致数据所有者**

如果您不希望您的模型或数据集等信息在本项目中被提及，或希望更新本项目有关的描述，请在GitCode提交issue，我们将根据您的issue要求删除或更新您相关描述。衷心感谢您对本项目的理解和贡献。

## License<a name="ZH-CN_TOPIC_0000002551989707"></a>

本项目的文档适用于CC-BY 4.0许可证，具体请参见[LICENSE文件](docs/LICENSE)。

## 贡献声明<a name="ZH-CN_TOPIC_0000002551909683"></a>

欢迎大家为社区做贡献，如果使用过程中有任何问题/建议，或者需要反馈特性需求和bug报告，可以提交[Issues](https://gitcode.com/boostkit/community/blob/master/docs/contributor/issue-submit.md)联系我们，具体贡献方法可参考[这里](https://gitcode.com/boostkit/community/blob/master/docs/contributor/contributing.md)。同时也欢迎大家在[讨论专区](https://gitcode.com/boostkit/community/discussions)展开讨论交流。感谢您的支持。
