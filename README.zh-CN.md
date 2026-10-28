# 2D Barcodes Generator（tdbg）

[English](README.md)

用纯 C99 从零实现的二维码生成库，支持 **QR Code、Data Matrix、Aztec、MaxiCode** 四种码制。没有任何第三方依赖，提供 C API、命令行工具和 Python 绑定（ctypes）。

## 目录结构

```
include/tdbg.h        公共 API（所有 FFI 都只需要这个头文件）
src/common.c          位缓冲、通用 GF(2^m) Reed-Solomon、符号对象、SVG 输出
src/qr.c              QR Code：版本 1-40，L/M/Q/H，数字/字母数字/字节模式，自动掩码
src/datamatrix.c      Data Matrix ECC200：24 种正方形 + 6 种长方形，ASCII/Base256 编码
src/aztec.c           Aztec：紧凑型 1-4 层、全尺寸 4-32 层，5 种字符模式 + 二进制移位
src/maxicode.c        MaxiCode：模式 2/3（结构化承运信息）、4、5、6，字符集 A-E + 数字压缩
cli/main.c            命令行工具
python/tdbg/          Python 包（ctypes 加载 C 库；纯 Python 输出 PNG，C 输出 SVG）
tests/test_tdbg.py    用独立解码器（zxing-cpp）做往返验证
tests/test_maxicode_orientation.py  MaxiCode 旋转任意角度、镜像、噪声下的定位与采样（maxicode_reader.py）
```

## 编译

```bash
make            # build/libtdbg.{so,dylib} + build/libtdbg.a + build/tdbg
```

或者用 CMake（Windows/MSVC 也适用）：

```bash
cmake -S . -B build && cmake --build build
```

## Python 使用

```bash
pip install .
```

```python
import tdbg

tdbg.qr("https://example.com", ecc="M").save("qr.png", scale=8)
tdbg.datamatrix("ABC-123", shape="rect").save("dm.svg")
tdbg.aztec("Aztec 码", ecc=33).save("aztec.png")
tdbg.maxicode("HELLO", mode=4).save("maxi.png", scale=10)

# UPS 风格的结构化 MaxiCode（模式 2：数字邮编）
tdbg.maxicode(secondary_bytes, mode=2, postcode="152382802", country=840, service=1)

sym = tdbg.qr("hi")
sym.width, sym.height, sym[0, 0]   # 直接访问模块矩阵
print(sym.to_text())               # 终端预览
```

不安装也能用：`make python` 会把动态库复制进 `python/tdbg/`，然后 `PYTHONPATH=python python3 ...`。也可以用环境变量 `TDBG_LIBRARY` 指定库的路径。

## 命令行

```bash
./build/tdbg qr -e H "hello" -o qr.svg
./build/tdbg datamatrix -r "ABC123" -o dm.pbm
./build/tdbg aztec -e 50 "text"
./build/tdbg maxicode -m 2 -p 152382802 -c 840 -k 1 "secondary message" -o maxi.svg
```

## C API

```c
tdbg_symbol *s;
int rc = tdbg_qr_encode((const unsigned char *)"hello", 5, TDBG_QR_ECC_M, 0, -1, &s);
if (rc == TDBG_OK) {
    /* s->modules[y * s->width + x] == 1 表示深色模块 */
    char *svg = tdbg_svg(s, 4.0, -1);
    tdbg_free(svg);
    tdbg_symbol_free(s);
} else {
    puts(tdbg_strerror(rc));
}
```

`tdbg_symbol.grid` 为 `TDBG_GRID_HEX` 时（MaxiCode），奇数行向右偏移半个模块，行距为 √3/2，中心需要绘制牛眼定位图形，`tdbg_svg()` 和 Python 的 PNG 渲染器都已经处理好了。

## 测试

```bash
pip install zxing-cpp pillow
make test
```

测试会生成 400 多个符号（涵盖每种码制的所有尺寸、所有纠错等级、二进制和非 ASCII 数据），再用 zxing-cpp 解码并逐字节比对。

## 说明

- 所有编码算法（Reed-Solomon、数据编码、掩码评分、Data Matrix 的模块布放、Aztec 的螺旋布局等）都是自己实现的。唯一的外部数据是 MaxiCode 的 33×30 模块位号表：它由 ISO/IEC 16023 图 5 定义，无法通过算法推导，转录自 ZXing（Apache 2.0），源码中已注明出处。
- 目前的编码器使用贪心模式选择，生成的码总是合法的，但不一定是最小尺寸。QR 的 Kanji 模式、Data Matrix 的 C40/Text/EDIFACT 模式、ECI 和结构化追加暂未实现。
