# Pulse 文档索引

按目的找，不按时间找。这里是入口；具体结论在各自的文件里。

| 我想… | 看这里 |
| --- | --- |
| 知道某个功能在哪些文件、对应哪个测试、是不是上游的 | [architecture.md](architecture.md) + [module-map.json](module-map.json) |
| 知道改完该跑哪个测试、哪些是发布门禁 | [testing-map.md](testing-map.md) |
| 在 `src/app` 的平铺文件里找某一组 | [src/app/INDEX.md](../src/app/INDEX.md) |
| 了解构建、安装、发布 | [README 的「从源码构建」](../README.md)、[installer-size-optimization.md](installer-size-optimization.md) |
| 做扩展 / 原生面板 | [extensions-sdk-quickstart.md](extensions-sdk-quickstart.md)、[sdk/native/README.md](../sdk/native/README.md) |
| 接 MCP / AI 应用 | [ai-pack.md](ai-pack.md)、[ai/mcp.md](ai/mcp.md) |
| 查诊断日志、崩溃、性能 | [diagnostics.md](diagnostics.md)、[PERFORMANCE_ROADMAP.md](PERFORMANCE_ROADMAP.md) |
| 看本地设计计划（栈安全等） | [design/stack-safety-plan.md](design/stack-safety-plan.md)、[design/memory-attribution-plan-20261110.md](design/memory-attribution-plan-20261110.md) |
| 看历史设计与交付记录 | [archive/](archive/)、按主题命名的 `*-2026-*.md` |

## 分类约定（只约束新增与本地文档）

- **顶层**：入口与仍在维护的活文档；已有的顶层文件不搬动，避免与上游冲突。
- **design/**：设计计划、取舍记录与界面 mockup；新增的本地设计放这里。
- **archive/**：过期交付记录、外部草稿与被取代的方案。
- **releases/、ai/、images/、video/**：沿用上游既有结构，不重排。

## 地图的维护

```powershell
python tools/ci/gen_module_map.py --write   # 改完模块后重新生成
python tools/ci/gen_module_map.py --check --strict   # 提交前验证没有未归类文件
```

- `module-map.json` 是生成物，不要手改；模块定义在 `tools/ci/gen_module_map.py` 的 `MODULES`。
- `testing-map.md` 同样由生成器写出，测试清单来自 `scripts/build_release_ci.ps1`。
- 本文与 `architecture.md` 是手写文档；只放稳定的阅读路线与规则。

