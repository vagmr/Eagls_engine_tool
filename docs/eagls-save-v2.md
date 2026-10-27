# EAGLS v2.0存档格式与诊断指南

## 样本、版本与验证边界

本profile以 **EAGLS v2.0** 代称。引擎信息界面与对话框资源自报 `EAGLS Ver 2.0`，PE文件/产品版本为 `1.5.0.0`；二者是不同的版本标注。机器标识为 `eagls-v2.0-pe1.5.0.0`，固定长度和偏移仅适用于已分析的这一构建，不能外推所有同版本引擎。

22份仅在本地保存的实际存档曾通过独立实现交叉比较。仓库只提供[从零生成的合成样例](../tests/fixtures/save_v2/README.md)，用于signed seed、连续随机数、旧尾部和结构往返测试；不包含游戏名称、游戏文件、真实存档或其解密状态。

已实现完整容器访问，不代表执行栈、变量、历史、解锁标志等每个状态字段已完全解释。其他EAGLS游戏和其他版本尚未验证。

## CLI 与库

```
eagls save info Save19.sav [--kind auto|global|numbered]
eagls save state Save19.sav --out work/save19.json [--kind auto|global|numbered] [--overwrite]
eagls save decrypt Save19.sav --out work/save19 [--kind auto|global|numbered] [--overwrite]
eagls save encrypt work/save19 --out work/Save19-copy.sav [--store] [--overwrite]
```

- `info`验证种类、密钥、解压长度，显示块大小、种子与旧尾部长度。
- `decrypt`同时导出明文压缩流 `blockN.lzss` 与解压状态 `blockN.bin`，以及 `header.bin`、`seed.bin`、`trailer.bin` 和 `manifest.json`。
- `encrypt`默认复用原压缩流，逐块核对其解压结果与`.bin`一致，因此未修改的归档可重建原文件。状态块改变时明确拒绝，需显式`--store`才全字面量重编码。
- `state`只读导出部分状态结构JSON：数值变量、字符串标识和长度（省略正文）、普通存档时间与执行游标、GlobalSave的32个原始设置字。详细字段与证据见[状态字段说明](eagls-save-v2-state.md)。
- `--store`不复现引擎的压缩token选择，文件较大。目标游戏r49修复环境中，原状态重编码，以及修改保存时间戳/字体边缘alpha后的加载、再保存、重读与退出均已通过；不代表任意状态编辑安全，也未证明未修复ISO程序的同等兼容性。
- 不覆盖输入或归档成员，包括`--overwrite`情况；既有输出默认拒绝。目录不递归扫描、不自动读取游戏目录。文件操作有64MiB工具输入上限；压缩流有2,000,000字节profile上限。
- 同一魔数同时用于全局和普通存档。自动识别会分别验证候选布局和固定解压长度，只有唯一合法候选才接受；可显式选择`--kind`，不依赖文件名。

公开接口在`include/eagls/save.hpp`：`decode_save`、`encode_save`不做文件操作；`inspect_save_file`、`decrypt_save_file`、`encrypt_save_archive`供CLI与后续GUI复用。

## 磁盘布局

整数均为小端。头128字节以以下ASCII魔数开头，其余头字节原样保留：

```
[E]nhanced [A]dventure [G]ame [L]anguage [S]ystem E.A.G.L.S
```

GlobalSave：

```
0x000  header[128]
0x080  uint32 compressed_size
0x084  encrypted_lzss[compressed_size]
       uint8 seed
       optional stale tail
```

普通存档：

```
0x000  header[128]
0x080  uint32 compressed_size0
0x084  uint32 compressed_size1
0x088  encrypted_lzss0[compressed_size0]
       encrypted_lzss1[compressed_size1]
       uint8 seed
       optional stale tail
```

GlobalSave解压为0x10B008（1,093,640）字节；普通两块**各自**解压为0x157400（1,405,952）字节。该固定长度来自目标引擎的复制长度，不是所有EAGLS版本的通用常量。当前真实样本第二块全零，但不能据此删除该块或认定其总是无意义；合成示例特意令第二块非零。

### 旧尾部：不能取物理文件最后字节作为种子

普通写档使用`CreateFileA(..., OPEN_ALWAYS=4, ...)`，没有截短旧文件。较短的新存档覆盖原文件后，物理末尾仍可能是旧内容。种子位置应为`header_end + sum(compressed_sizes)`。

| 本地样本 | 忽略但归档保留的旧尾部 |
| --- | --- |
| Save0.sav | 286字节 |
| Save1.sav | 3,105字节 |
| Save20.sav | 1,651字节 |
| Save28.sav | 788字节 |

无损重建必须保留它们。解压、运行时状态解析则不能把它们并入压缩流。

## XOR：46字节池、带符号种子、连续随机数

密钥池（不包括终止NUL）：

```
1qaz2wsx3edc4rfv5tgb6yhn7ujm8ik,9ol.0p;/-@:^[]
```

长度是**46**，末尾`]`不能漏掉。

1. 存储的seed先按signed char符号扩展，再转为32位无符号状态传给MSVC `srand`。
2. 每字节推进`state = state * 214013 + 2531011`（模2^32），`rand = (state >> 16) & 0x7fff`。
3. 输入字节异或`pool[rand % 46]`；加密/解密同算法。
4. 普通第一块处理完后**不重置随机数**，第二块继续。

独立Windows CRT已知答案：seed字节195 → `srand(uint32(-61))`，前八个密钥字节为`2f 36 6f 6a 32 39 6a 6d`。错误地使用unsigned seed=195将产生不同结果。

## LZSS，而非LZHUF

- 4096字节零环，写入起点0xFEE。
- 每个flag的低位先行；1=单字节literal，0=两字节match。
- `offset = lo | ((hi & 0xf0) << 4)`，`length = (hi & 15) + 3`，长度3～18。
- offset是环的绝对索引；逐字节复制，允许源/目标重叠。
- 每个压缩块重置环；跨块连续的是XOR随机数状态。

诊断规则：大量重复字节本身不能证明解压正确。必须同时满足固定输出长度、结构字段合理、独立实现一致和原文件逐字节重建；不要用LZHUF替代该profile的LZSS。

## 引擎函数定位

以下为目标32位EXE的VA（ImageBase=0x400000），不是其他版本的通用补丁地址。

| 功能 | VA |
| --- | --- |
| 全局写 / 读 | 0x41F0B0 / 0x41F310 |
| 普通写 / 纯读取 | 0x41F500 / 0x41F820 |
| 应用状态并重建对象 | 0x41FAF0 |
| LZSS解压 / 压缩 | 0x404D10 / 0x4085E0 |
| 恢复时的字体构造 | 0x40D790 |
| CreateFontIndirectA导入槽 | 0x48204C |

全局写出从VA0xC821B0复制0x10B008字节；普通两块从VA0xD8D1F8、0x52A978各复制0x157400字节。纯读取与“应用状态”是不同函数，菜单列出存档不等于真正加载游戏状态。

## 字体回退问题的证据链

普通第一状态块的字体表从偏移`0x79808`开始，每对象`0x178`（376）字节。部分已核实字段：

| 对象内偏移 | 意义 |
| --- | --- |
| 0x08 / 0x0C / 0x10 | 字重、italic参数、逻辑字号 |
| 0x20 / 0x24 / 0x28 | 恢复构造所用的排版/间距参数，精确字段名尚未全部确定 |
| 0x30 | NUL结尾ANSI/CP932字体名，32字节区域 |
| 0x94 | 保存时HFONT值，跨进程不可当成可复用句柄 |
| 0xA0 / 0xA4 / 0xA8 | 字形边缘alpha / 描边分支开关 / 描边alpha乘数；不是RGB颜色 |

`GlobalSave`偏移`0xC01FC`对应VA`0xD423AC`，保存字体组偏移。设置处理代码在0x405F43写0（黑体组），0x405F59写`MaxFont/2`（宋体组）；正文绘制0x40B7BD、0x40BB59等处直接将其加到基础字体编号。

旧表可见布局：黑体角色0～10、宋体11～20、特殊黑体21，之后槽为空；当前配置`MaxFont=30`，字体组起点却是15。旧宋体常规槽11字号18，而旧槽15字号12。旧表被恢复后，偏移15选中了小字角色。

现场日志独立验证了这一点：

| 状态 | 宋体槽15逻辑字号 | Windows最终字形 | 实际高度 |
| --- | --- | --- | --- |
| 读档前 | 21 | 新宋体（NSimSun） | 28像素 |
| 旧表直接恢复后 | 12 | 新宋体（NSimSun） | 16像素 |
| 本地布局迁移并保留当前排版后 | 21 | 新宋体（NSimSun） | 28像素 |

迁移策略：只调整临时读档缓冲区，保留当前排版，不改写原存档。目标游戏中宋体、字号与黑体切换已验证；此版本特定运行时补丁不属于通用存档codec。

诊断规则：不能根据外观推断字体族。CreateFont请求之外，还须在应用后以GetTextFace/GetTextMetrics核对实际字形和高度；应分别检查全局组偏移、局部布局和对象重建路径。

### 可复用的调查顺序

1. 记录实际CreateFile路径，区分读与写，避免分析了另一安装目录。
2. 先验证容器长度、解密参数、解压算法和固定输出长度。
3. 比较状态字段；字体对象不仅保存名字，也保存字号、间距和旧进程句柄。
4. 同时检查全局选择器与局部字体表，不能仅修改字体文件名或注册表。
5. 观察引擎“重建运行时对象”后的实际结果，再做保守且幂等的迁移。

## 测试与独立复验

默认CTest包括`core.save`、`cli.contract.save`与`cli.save_roundtrip`。覆盖两种布局、signed seed已知向量、双块连续随机数、重叠/零环解压、旧尾部、原始压缩流保留、显式修改块重编码、损坏长度/种子、中文路径、拒绝覆盖及同文件保护。

真实样本不提交、不自动读取；可显式选择使用者自己的本地样本：

```
cmake -S . -B build/local -DEAGLS_LOCAL_SAVE_DIR="独立的存档样本目录"
cmake --build build/local --config Release
ctest --test-dir build/local -C Release --output-on-failure
```

不自动寻找游戏、不改注册表、不运行游戏。`core.save_local`只读样本并在内存做逐字节往返。自动测试或合成状态读回成功不等于任意修改后的游戏状态可正常运行。
