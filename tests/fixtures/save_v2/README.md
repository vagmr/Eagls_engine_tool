# EAGLS v2.0存档合成示例 / synthetic save examples

本目录所有二进制均由 [generate.py](generate.py) 从零构造，未复制游戏程序、剧情、图片、个人进度或任何实际存档内容。它们符合已验证的存档**容器**，并非能在游戏中加载的有效剧情状态。

适用profile：**EAGLS v2.0**，PE文件/产品版本1.5.0.0。版本仅限定已验证构建，不保证所有同版本程序的状态布局一致。

| 目录 | 内容 | 预期 |
| --- | --- | --- |
| `global/example.sav` | 加密的合成 GlobalSave 格式，seed=83 | 一块，解压0x10B008字节，全局字体组偏移0xC01FC=15 |
| `global/decrypted/` | 头部、种子、明文LZSS和解压状态、旧尾部、manifest | CLI `save decrypt` 的二进制输出逐字节匹配 |
| `numbered/example.sav` | 加密的双块合成普通存档，seed=195（符号扩展为-61） | 两块各0x157400字节，第二块继续第一块的随机序列 |
| `numbered/decrypted/` | 对应解密/解压归档 | 包含22槽旧字体布局的合成技术字段，其他数据以零为主 |

两例均有明确标记的旧尾部；物理文件最后的0xFF不是种子。解码必须依声明长度找种子。第二块有不同合成标记，能检测随机数被错误重置。

完整 SHA-256 在 [SHA256SUMS.json](SHA256SUMS.json)。生成器只用 Python 3.10+ 标准库，是维护者重现示例的可选工具；构建、CTest及实际CLI均不依赖Python。生成器独立实现零串LZSS编码与MSVC随机数异或，不调用C++。

```
python tests/fixtures/save_v2/generate.py
eagls save info tests/fixtures/save_v2/numbered/example.sav
eagls save decrypt tests/fixtures/save_v2/numbered/example.sav --out work/save-example
eagls save encrypt work/save-example --out work/example-rebuilt.sav
```

最后产物的SHA-256应等于原始`example.sav`。默认保存原始压缩流，避免把“解压后相同”误称为“文件完全相同”。`--store`支持显式全字面量编码；对修改状态的游戏内加载尚未验证。

详细格式、函数地址、版本证据及字体问题的排查经验见 [存档经验包](../../../docs/eagls-save-v2.md)。
