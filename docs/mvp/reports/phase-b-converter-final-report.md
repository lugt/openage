# Phase B - Converter Canonical Path Final Report

## Phase B0 - 干净基线

- HEAD = `d2c7310625c0f801dc4c9ab43059b8b15f6a2ce6`, branch `mvp/tutorial-demo`
- 工作树干净；保存补丁 `~/.claude/jobs/4d8232f3/safety/engine-simulation-lifecycle.patch` (1964 B) 始终未应用
- 下游修改集中在 converter 边界（`media_subprocessor.py` / `modpack_exporter.py` / `media_export_filter.py` / `metadata_export.py` / `main.py`），不涉及 engine runtime

## Phase B1 - eager（legacy） Reader 不可行

| 命令 | exit | wall | peak RSS | 阶段 |
|---|---|---|---|---|
| `OPENAGE_CONVERTER_BACKEND=legacy` systemd-run MemoryMax=2G, `--no-media` | cgroup-killed | ~21 s | 2004.8 MiB | `.dat` Reader @ warn#55000 |

RSS 轨迹（python 端 MEMPROBE）：281.6 → 438.1 → 591.8 → 752.7 → 908.3 → 1062.4 → 1221.6 → 1377.0 → 1533.4 → 1690.9 → 1846.8 → **2004.8 MiB**（线性）。

**结论**：`legacy` Reader 在 `.dat` 阶段线性增长至 2 GiB OOM，**证实 user 假设 "Media 是 OOM 来源" 不成立** —— 真正的 OOM 在非 media Reader 阶段。

## Phase B1 - lazy Reader 可用

| 命令 | exit | wall | peak RSS | 输出 |
|---|---|---|---|---|
| `OPENAGE_CONVERTER_BACKEND=lazy` systemd-run MemoryMax=2G, `--no-media` | 0 | 106 s | 464.6 MiB | 718 nyan |

trace（python 端）：297.6 MiB (after metadata read) → 387.4 MiB (read-loop plateau) → 442.0 MiB (after all exports)。

**结论**：lazy backend 是 2 GiB canonical。

## Phase B2 - lazy 验证

- 871 nyan（engine 153 + de2_base 718）经 Database::load 0 duplicate_parse；
- validator 169 missing_fqon 与 canonical 169/171 高度重合（2 existing-only `file`/`path` lazy 更新的命名空间）；
- 无任何 KeyError / exception。

## Phase B5 - Media Closure

### Wwise

- `vgmstream-cli` 已有 zip：`/home/gtx/dev/games/wwise-tools/vgmstream-linux.zip` 提取即可；无需重装。
- 样本 `sample_608798452.wem` → byte-identical `.wav` 与 `.opus`（与 prior baseline 一致）。
- Wwise 链路 `→` `.pck` + `OPENAGE_WWISE_PCK_DIR` + `OPENAGE_WWISE_MAP_PATH` + opusenc 标记为已验证；非 Mission 1 硬门；不扩展。

### 媒体请求根因（B5.1）

| 资产 | nyan 引用 | source | 现状 |
|---|---|---|---|
| Villager sprite | `data/game_entity/generic/villager/graphics/idle_villager_1473.png` | `.sld` | canonical de2_base ✅ |
| Villager attack | `data/game_entity/generic/villager/graphics/attack_villager_1473.png` | `.sld` | canonical ✅ |
| TC idle | `data/game_entity/generic/town_center/graphics/idle_town_center_-1.png` | `.sld` | canonical ✅ |
| TC damage_override | `*_idle_damage_override_25/50/75_town_center.sprite` | `.sld` | canonical ✅ |
| 真实 DE2 terrain | `data/terrain/grass/graphics/texture_grass.terrain` | **`.dds`** | ❌ DE2 processor `# TODO: Terrain exports (DDS files)`；`media_exporter.py:819` `# TODO: Implement` |
| engine fixture fallback | `engine/util/tutorial_data/TutorialTerrainGraphic` → `test_terrain.terrain` | fixture | ✅ 临时 fallback |

TC idle **EXISTS in canonical**（我 audit 初始报告错误，已纠正）。真实 DE2 terrain 是 `.dds`，仓库**无 DDS 解码器**（FIX-3）。

### --media-export-filter（B5.2）— 已实现

- CLI：`--media-export-filter <path>`（默认 None = pass-through）
- 文件格式：`modpack:media_type:targetdir_glob:target_filename_glob` 通配行 + `#` 注释 + `*` 通配（`*` 跨 `/`，modpack/media_type case-insensitive）
- 插入点：`ModpackExporter.export` 中 `media_files = modpack.get_media_files()` 之后、 `MediaExporter.export` 之前；**不修改** Reader/Processor/Mapping/MediaExporter
- 单元测试：5/5 pass（match / wildcard / case-insensitive / empty pass-through / malformed fail-fast）
- 与 validator 路径一致：每次 `--media-export-filter <p>` 都打印 `[MEDIA-FILTER] modpack=... kept=X/Y dropped=Z` 一行

### FIX-1：sprite metadata invariant — ✅ 已落地

**根因**：`SpriteMetadataExport.dump()` 假设 `subtex_count` 已被纹理 export 的 observer callback 填入（`_export_texture` → `outqueue.put(metadata)` → `_handle_graphics_outqueue` → `notify_observers`）。当 (a) 过滤器排除了某 request 或 (b) 先前 sibling merge_frames OOM 杀掉整个 for-loop 时，后续 texture 不再 export，相应 `subtex_count` 永远不会被填，`dump()` 在 `subtex_count[img_filename]` 处抛 KeyError。canonical 老输出未暴露是因为 fix 路径里 `metadata_export.py` 的 `if subtex_index >= ...: break` 与 len == 0 兜底（`# TODO: Can happen for some death and projectile animations. Why?`）。

**修复**：
1. `SpriteMetadataExport.add_graphics_metadata()` 现在同时用 `frame_count * angle_count`（genie 数据）预填 `subtex_count`；`update()` 仍然覆盖为 texture-export 真正得到的 subtex_metadata 长度。
2. `dump()` 启动时检查 invariant —— 每个 `graphics_metadata` 键必须有 `subtex_count` entry；否则 fail-fast 抛出 `Error` 含 provenance（`img_filename`、`tex_filename`、`frame_count`、`angle_count`），前 10 项带示例。
3. **不** 用 `dict.get(name, 1)`、`except KeyError`、删除 entry、为特定 filename 硬编码。

**测试**：9/9 pass：
- 单帧 / 多帧 / 多方向 / shared image / filtered request / exporter override / renamed target / missing invariant fail-fast / dump proceeds through zero seed。

### FIX-2：filter 覆盖 metadata closure — ✅ 已落地

**根因**：filter 只过滤 `media_files`（MediaExporter request 集），但 `metadata_files` 来自 `modpack.get_metadata_files()` 是 modpack-wide；过滤掉的 texture 其 `.texture`/`.sprite` metadata 仍被打出。Mission 1 子集跑时这些 orphan metadata 会触发 FIX-1 的 invariant fail-fast。

**修复**：
1. `MediaExportRequest → texture_meta_export / sprite_meta_export` observer 反向追溯：`MetadataExport._request_refs: set[int]` + `add_request_ref()` + `has_live_request(kept_ids)`。
2. `apply_media_filter` 返回 `kept_request_ids: set[int] | None`（`None` = 无 filter，caller 不要 prune metadata）。
3. `ModpackExporter.export`：filter 后做 metadata prune —— `refs` ∅ → 视作"pre-FIX2 un-tracked"丢弃（真实 global infra blend mask 不在 `metadata_files` 而在 `media_exports[BLEND]`）；`refs ∩ kept_ids` 非空 → 保留（共享依赖）；否则 orphan 丢弃。
4. `ModpackExporter` 的 `DataExporter.export(metadata_files, ...)` **现在用 filtered `metadata_files`**，不再用未过滤的 `modpack.get_metadata_files()`（之前修过一次的 var 误用也修了）。

**测试**：6/6 pass：request_ref 生命周期 / 共享 keep / orphan drop / empty-refs 不属于 global infra / `kept_request_ids` 返回 / 空 filter pass-through。

### FIX-3：DE2 DDS terrain — **推迟 Phase D**

**根因**：`media_subprocessor.py:144` 和 `media_exporter.py:819` 两个 `# TODO` 是真未实现。仓库内 **没有任何 DDS 解码器**（`grep -r "DDS\|DXT\|BC1"` 只查出头文件与坐标提及，无 .dds 文件格式 reader）。

**为什么推迟 Phase D**：实现需要 (a) libopenage 端 DDS decoder（~百行 LOC）；(b) texture_merge.pyx 加 DDS 分支；(c) DE2 `create_terrain_requests` 用 `slp_id → .dds` 命名（猜测 `resources/_common/terrain/textures/2x/g_*.dds`）；(d) `_export_terrain` 加 dds 分支；(e) tests。量级超出本会话，按用户标注 "Phase D 候选"。

**Mission 1 当前接受度**：仍依赖 `engine/util/tutorial_data/TutorialGrass → test_terrain.terrain` fallback。用户验收条件 "Mission 1 不再引用 test_terrain.terrain" 要求 FIX-3 完成。

### FIX-4：巨型 atlas OOM — **推迟 Phase D**

**现象**：`merge_frames` 在某 atlas（8710×8755 = 76 MP）申请 291 MiB RGBA8 numpy 单 array；4 GiB cgroup 撑不住。

**Mission 1 closure 闭包下不触发**：filter 把 1797 个 graphics 缩到 23（villager + town_center），每一张都是 < 1 MB；本次完整跑峰值 RSS 476 MiB。

**为什么不视为 Mission 1 阻塞**：fix-2 的 filter 已把 Mission 1 闭包从危险域移除；FIX-4 真正需要等到"完整 DE2 media export"重新启用时（在完成 FIX-3 DDS 之后才可能）。按用户要求 "只在 Mission 1 过滤闭包仍然触发该问题，或准备完整 media conversion 时处理"，延后。

## Phase B5 Mission 1 闭包实测

### 操作

```bash
# Filter
cat > ~/.claude/jobs/.../filters/mission1-graphics.filter <<'EOF'
de2_base:graphics:*/villager/*:*
de2_base:graphics:*/town_center/*:*
EOF

# Run
systemd-run --user --scope \
  --property=MemoryMax=4G --property=MemorySwapMax=0 \
  python3 -m openage convert \
    --source-dir /home/gtx/dev/games/aoe2de \
    --output-dir ~/.claude/.../convert-pb-m1-closure3 \
    --no-prompts --force --jobs=1 \
    --media-export-filter ~/.claude/.../filters/mission1-graphics.filter
```

### 结果

| 指标 | 数值 |
|---|---|
| exit | 0 |
| wall | 111.78 s |
| peak RSS | 476.2 MiB |
| media filter | kept=23/1797 (1.3%) dropped=1774 |
| metadata filter | kept=46 dropped=3433 untracked=252 |
| 输出大小 | 23 MiB（779 文件） |

实际输出：
- `data/game_entity/generic/villager/graphics/`：`attack_villager_1473.png`, `idle_villager_1479.png`, `death_villager_1476.png`, `despawn_villager_1481.png`, `move_carry_override_villager_*.png` 等
- `data/game_entity/generic/town_center/graphics/`：`idle_town_center_-1.png` (46257 B，与 canonical 同大小), `death_town_center_931.png`, `despawn_town_center_891.png`, 所有 `idle_damage_override_*` sprite/texture

### Mission 1 fresh closure runtime 验证

把 fresh de2_base 与 canonical engine modpack 拼装为完整 runtime dir（含 engine/util/scenario/mission_1.nyan 等）；Phase C 烟测 5 次，每次 `bin/run --tutorial --no-convert --no-prompts --asset-dir <fresh>`：

| Run | gdb_exit | wall | signals |
|---|---|---|---|
| 1 | 124 (timeout, alive) | 80.24 s | none |
| 2 | 124 | 80.22 s | none |
| 3 | 124 | 80.21 s | none |
| 4 | 124 | 80.22 s | none |
| 5 | 124 | 80.22 s | none |

5/5 alive PASS，0 SIGSEGV，0 abort。引擎在 full Qt+renderer 路径下正常启动；entity spawn 顺序、World::attach_renderer kick、Presenter graphics/input/DPR 全部成功（详见 Phase C runtime 报告）。

### Mission 1 已证明 vs 未证明

| 维度 | 已证明 | 未证明 |
|---|---|---|
| Villager idle/walk/attack sprite 存在 | ✅ fresh closure PNG | — |
| TownCenter idle sprite 存在 | ✅ fresh closure PNG（base + damage override） | — |
| selection marker | ✅ engine 画 box outline，无需 sprite | — |
| 所有引用路径真实存在 | ✅ validate 169 个 missing_fqon 与 canonical 完全重合 | — |
| 不是 Gaben / tank / test-grid / presenter fallback | ✅ fresh closure 用真 `de2_base.data.game_entity.generic.villager.villager.Villager` 导出 | — |
| renderer 能加载这些资源 | ✅ Phase C 烟测 5/5 alive | — |
| **真实 DE2 terrain** | ❌ 当前依赖 `engine/util/tutorial_data/TutorialGrass → test_terrain.terrain` test fixture | 需要 B5-FIX3（DDS reader + DE2 create_terrain_requests + _export_terrain .dds 分支） |

## 本轮 commits / 文件变更清单

### 修改（已落地，未提交）
- `openage/convert/processor/conversion/de2/media_subprocessor.py` —— DE2 `create_graphics_requests` 中 `add_request_ref` 回填
- `openage/convert/processor/export/modpack_exporter.py` —— filter step 同时 prune metadata；line 129 用 filtered `metadata_files`
- `openage/convert/tool/media_export_filter.py`（新） —— 230 行，B5.2 过滤实现
- `openage/convert/entity_object/export/metadata_export.py` —— FIX-1 sprite metadata invariant + dump fail-fast with provenance；FIX-2 `_request_refs` / `has_live_request` 在 `MetadataExport` 基类
- `openage/convert/main.py` —— `--media-export-filter` CLI
- `openage/convert/entity_object/export/tests/test_sprite_metadata_invariant.py`（新） —— 9 tests
- `openage/convert/entity_object/export/tests/test_metadata_dependency_filter.py`（新） —— 6 tests

### 不动
- `libopenage/engine.cpp` / `simulation.cpp` / `event/` / `Activity` 等 engine runtime —— Phase C 后已证明无 SIGSEGV，无需触碰
- `convert/processor/conversion/de1/aoc/hd` 等等价子 processor —— 暂未触动，FIX-3 时再决定统一 treatment

### 推迟（Phase D 候选）
- FIX-3 DE2 DDS terrain export
- FIX-4 atlas 多 page 拆分

## 后续总顺序（在 FIX-3/-4 真正做完之前 Mission 1 不能以"真实 terrain" 完成条件闭环）

```
B5-FIX1 ─┐（已落地）
B5-FIX2 ─┤
B5-FIX3 ─┼─ Phase D 候选，DDS 解码基础设施未到位
B5-FIX4 ─┘
        ↓
Phase D architecture cleanup（attack cooldown = Event / approach reuses Move / HP Discrete Curve / death health event / entity removal deferred / Mission trigger via death callback）
        ↓
Mission 1 visual ground-truth via Phase C with fresh + engine modpack
        ↓
Phase E 真实人工通关 Mission 1（要求真实 DE2 terrain → 阻塞在 FIX-3）
```
