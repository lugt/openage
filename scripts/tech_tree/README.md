# 本机 DE2 文明与科技树导出

从已安装游戏的 `empires2_x2_p1.dat` 批量导出全部文明、单位、科技、效果和原始科技树连接。当前验证的原版文件为 `VER 8.9`，包含 Gaia 和 62 个玩家文明、1,510 条科技记录、1,500 个效果组。

## 运行

在项目根目录运行。

```sh
venv/bin/python3 -m pip install -r scripts/requirements-tech-tree.txt
venv/bin/python3 scripts/export_tech_trees.py
```

默认读取本机 Steam 的 `AoE2DE/AgeOfEmpires2Data`，输出到 `logs/tech-trees`。双击输出的 `index.html` 即可查看，整个输出目录需要一起保留。

```sh
venv/bin/python3 scripts/export_tech_trees.py \
  --original-dir '/path/to/AgeOfEmpires2Data' \
  --output logs/tech-trees
```

`--dat` 可指定该安装目录下的另一份 DAT。中文、英文名称和图标仍从 `--original-dir` 读取。

## 导出内容

- `manifest.json` 记录来源 SHA-256、逐文明原始档案 SHA-256、记录数量和缺失图标。
- `tech-trees.json` 包含轻量文明索引和全部科技的条件图，包含隐藏科技。
- `civilizations/*.json` 包含每个文明的预览节点、原始 ID、科技禁用列表、费用、时间、单位基础属性、动画引用和科技效果。
- `civilizations/*.js` 用于本地 HTML 按需加载当前文明。切换时释放前一个文明的预览对象。
- `raw/civilizations/*.json.gz` 保存包括 Gaia 在内的全部文明原始字段和全部单位槽位，包含场景实体、不可用单位和空槽位。
- `raw/technologies.json.gz` 保存全部科技字段，包括原始六个前置槽位、N 项门槛和全部研究位置。
- `raw/effects.json.gz` 保存全部效果组和原始命令。
- `raw/tech_tree.json.gz` 保存原版时代、建筑、单位和研究连接。
- `raw/graphics.json.gz`、`raw/unit_headers.json.gz`、`raw/globals.json.gz` 保存其余 DAT 表。图像表保留原始空槽位。
- `raw/civilization_metadata.json.gz` 保存官方文明清单。
- `export-performance.json` 记录本次耗时和导出进程的峰值 RSS。

## 内存处理

参考项目 `openage/convert/value_object/read/source.py` 的 mmap 和偏移索引思路。当前导出适配器使用支持 `VER 8.9` 的上游 `genieutils-py==0.1.2` 字段读取器，复用其完整版本布局。

压缩输入按 1 MiB 分块解压到临时文件，通过只读 mmap 访问。第一遍只保留文明偏移，每次解析一个文明并释放。科技表位于文明数组之后，第二遍据偏移逐文明导出。JSON 逐字段写入 gzip，每个文明独立落盘。预览只加载当前文明。

全量导出的末尾检查读取字节数与解压文件长度相等。临时文件和 mmap 在正常退出及异常路径中关闭。输出文件通过临时文件替换，原始档案保留所有已解析字段。

## 预览计算

预览按科技的文明限制、禁用状态、原始 N 项前置条件、建筑落成科技、单位启用和升级关系计算潜在可达性。包含自动初始化对区域兵种的禁用，以及文明的 Modify Tech 重新启用命令。时代计算使用科技前置和原始连接里的时代槽位。

节点连线展开隐藏前置，点击详情仍显示原始 N 项门槛。多条可选前置可能同时出现在连线中。

单位属性为 DAT 基础值，科技费用与时间包含直接文明科技树调整。局内属性变化、团队组合、战役触发器和全部效果命令的执行，还需要由游戏运行时应用。所有效果命令均已原样导出。此命令生成可检查的数据目录与预览，游戏所用 nyan 模组的生成和执行需要继续接入当前版本的数据。

当前已安装的 nyan 模组可通过引擎单独审计。

```sh
ninja -C bin -j8
venv/bin/python3 scripts/export_tech_trees.py --audit-runtime
```

该选项调用原生 demo 52，独立加载每个已安装文明的 setup patch，保存 `runtime-audit.json` 和 `runtime-native.json.gz`。报告中的文明数量用于核对安装包覆盖范围。

## 回归检查

```sh
venv/bin/python3 -m pytest scripts/tests/test_tech_tree_export.py -q
```

覆盖 N 项门槛、建筑落成条件、文明隔离、循环前置、升级来源一致性、区域科技重启用、流式解压边界、输入损坏、逐文明释放和确定性 JSON 输出。

数据解析依赖来自 [genieutils-py](https://github.com/SiegeEngineers/genieutils-py)。Modify Tech 字段编号依据 [Advanced Genie Editor 的效果命令定义](https://github.com/Tapsa/AGE/blob/master/AGE_Frame/Techs.cpp)。
