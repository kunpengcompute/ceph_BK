# Release Notes

## Version Mapping<a name="EN-US_TOPIC_0000002521463536"></a>

### Product Version Information<a name="EN-US_TOPIC_0000002521463606"></a>

<table><tbody>
<tr>
    <th>Product Name</th>
    <td>Kunpeng BoostKit</td>
</tr>
<tr>
    <th>Product Version</th>
    <td>25.3.0</td>
</tr>
<tr>
    <th>Feature Name</th>
    <td>
        <ul>
            <li>Ceph SPDK I/O acceleration</li>
            <li>Ceph EC Turbo enablement</li>
            <li>Ceph EC Turbo tuning</li>
            <li>Ceph RDMA network acceleration</li>
            <li>Ceph data compaction</li>
        </ul>
    </td>
</tr>
</tbody></table>

### Software Version Mapping<a name="EN-US_TOPIC_0000002521623612"></a>

#### Ceph SPDK I/O Acceleration

| Item   | Version                               |
|-------|-----------------------------------|
| OS | openEuler 20.03 LTS SP4           |
| Ceph  | 17.2.7                            |
| UCX   | 1.14.1                            |
| SPDK  | 21.01.1 (openEuler 22.03 LTS SP4) |
| DPDK  | 21.11 (openEuler 22.03 LTS SP4)   |
| ISA-L| 2.30.0 (openEuler 22.03 LTS SP4)  |

#### Ceph EC Turbo Enablement

| Item  | Version                     |
|------|-------------------------|
| OS| openEuler 20.03 LTS SP1 |
| Ceph | Ceph 14.2.8             |

#### Ceph EC Turbo Tuning

| Item  | Version             |
|------|-----------------|
| OS| openEuler 20.03 |
| Ceph | 14.2.10         |

#### Ceph RDMA Network Acceleration

| Item  | Version                     |
|------|-------------------------|
| OS| openEuler 20.03 LTS SP4 |
| Ceph | 14.2.8                  |
| UCX  | 1.14.1                  |

#### Ceph Data Compaction

| Item  | Version                                                        |
|------|------------------------------------------------------------|
| OS| CentOS Linux release 7.6.1810 <br> openEuler 20.03 LTS SP1 |
| GCC  | GCC version 7.3.0                                          |
| Ceph | Ceph 14.2.8                                                |

### Hardware Version Mapping<a name="EN-US_TOPIC_0000002552663559"></a>

#### Ceph SPDK I/O Acceleration

| Item | Description            |
|-----|----------------|
| CPU | Kunpeng 920      |
| NIC | 2 x 25GE x 2|

#### Ceph EC Turbo Enablement

| Item   | Description          |
|-------|--------------|
| CPU| Kunpeng 920|

#### Ceph EC Turbo Tuning

| Item   | Description                                                  |
|-------|------------------------------------------------------|
| CPU| Kunpeng 920                                            |
| Memory | 12 x 32 GB                                             |
| NIC   | 4 x 25GE IN200 NIC                                   |
| Drive   | System drive: 960 GB SATA HDD<br>Data drive: 8 x ES3000 V5 3.2 TB NVMe SSD|

#### Ceph RDMA Network Acceleration

| Item | Description                                                                   |
|-----|-----------------------------------------------------------------------|
| CPU | Kunpeng 920                                                             |
| NIC | Client node: one 2 x 25GE NIC, 50GE in total<br>Ceph node: one 4 x 25GE NIC or two 2 x 25GE NICs, 100GE in total|

#### Ceph Data Compaction

| Item   | Description      |
|-------|----------|
| CPU| Kunpeng 920|

### Virus Scan Results<a name="EN-US_TOPIC_0000002521463602"></a>

Virus scan is not involved because no software package is released.

## Ceph SPDK I/O Acceleration

### v1.0.0

#### Change Description

This is the first official release.

A Ceph cluster is deployed in containers on Kunpeng servers running openEuler 20.03. The integration of the SPDK, UCX, and KSAL maximizes storage and network performance, answering the need for high throughput and low latency in modern distributed storage.

#### Resolved Issues

None

#### Known Issues

None

## Ceph EC Turbo Enablement

### v1.0.0

#### Change Description

This is the first official release.

Enabling EC on Ceph improves the I/O read and write performance for data whose length is within a stripe.

#### Resolved Issues

None

#### Known Issues

None

## Ceph EC Turbo Tuning

### v1.0.0

#### Change Description

This is the first official release.

After the EC Turbo feature is enabled, the read and write performance of a 4 KB block cluster in EC mode can be optimized through EC Turbo tuning.

#### Resolved Issues

None

#### Known Issues

None

## Ceph RDMA Network Acceleration

### v1.0.0

#### Change Description

This is the first official release.

This feature deploys Ceph on a Kunpeng server running openEuler 20.03 and configures the UCX network framework. It uses UCX to enable the RDMA network and optimize the front- and back-end networks of the Ceph cluster.

#### Resolved Issues

None

#### Known Issues

None

## Ceph Data Compaction

### v1.0.0

#### Change Description

This is the first official release.

This feature is deployed on an open-source Ceph cluster to eliminate data waste caused by zero padding. In addition, combined with techniques including data encapsulation, space allocation based on block counting, granularity-based traffic steering, batch submission, and batch callback, the data compaction feature improves the data reduction ratio and overall system IOPS, which reduces costs and improves performance.

#### Resolved Issues

None

#### Known Issues

None

## Related Documentation

### Documentation

| Document Name            | Description                         | Delivery Method|
|------------------|-------------------------------|------|
| Ceph Feature Release Notes  | Provides the BoostKit Ceph feature release information. | Open-source repository |
| Ceph SPDK I/O Acceleration Feature Guide| Describes how to install and deploy the Ceph SPDK I/O acceleration feature.| Open-source repository |
| Ceph EC Turbo Feature Guide | Describes how to install and deploy the Ceph EC Turbo feature. | Open-source repository |
| Ceph EC Turbo Tuning Guide | Describes how to tune the Ceph EC Turbo feature.    | Open-source repository |
| Ceph Data Compaction Feature Guide     | Describes how to install and deploy the Ceph data compaction feature.      | Open-source repository |
| Ceph RDMA Network Acceleration Feature Guide | Describes how to install and deploy the Ceph RDMA network acceleration feature. | Open-source repository |

### Obtaining Documentation

Visit the [open-source repository](https://gitcode.com/boostkit/ceph_BK) to view or download related documents.

## Change History

| Date | Description      |
|-------|----------|
| 2025-03-30 | This is the first official release.|
