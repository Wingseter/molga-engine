# Asset GUID Runtime Catalog Plan

> Date: 2026-06-28
> Track: P1 runtime asset identity and import pipeline.

## Problem

Asset GUIDs exist, but runtime packages still depend on path fallback and do not load a read-only asset catalog. The editor scans assets and builds an in-memory database, while runtime only sets asset root and calls `ResolveAssets`.

Current evidence:

- Editor project load scans `AssetDatabase`: `src/main.cpp:210-215`.
- Runtime only sets asset root and resolves components after scene load: `src/runtime_main.cpp:211-212`.
- `AssetDatabase::AbsoluteSourcePath` depends on in-memory records: `src/Core/AssetDatabase.cpp:112-115`.
- `AssetDatabase::ScanProject` rebuilds maps and can create/import metadata: `src/Core/AssetDatabase.cpp:73-100`.
- `SpriteRenderer` serializes `textureGuid` but falls back to `texturePath`: `src/ECS/Components/SpriteRenderer.cpp:72-141`.
- Missing texture fallback points at `Editor/missing_texture.png`: `src/Core/AssetDatabase.cpp:149-150`.
- `GameBuilder` copies `Assets`, `Shaders`, `Scenes`, and executable, but no asset catalog or runtime placeholder resource: `src/Editor/GameBuilder.cpp:221-373`.

## Target Architecture

GUID is the authoritative asset reference. Path is only for legacy migration, display, and development fallback.

Package artifact:

```text
asset_catalog.json
Assets/...
Resources/missing_texture.png
```

Initial catalog schema:

```json
{
  "schemaVersion": 1,
  "assetRootMode": "packageRoot",
  "records": [
    {
      "guid": "...",
      "sourcePath": "Assets/Textures/player.png",
      "importer": "TextureImporter",
      "importerVersion": 1,
      "artifactPath": "",
      "hash": "...",
      "width": 64,
      "height": 64
    }
  ]
}
```

`AssetDatabase` modes:

- Editor/build: `ScanProject()` creates/loads `.meta` and runs importers.
- Runtime: `LoadCatalog()` reads a packaged JSON and never creates `.meta` or writes import artifacts.

## Phases

### Phase 1. Canonical Path Rules

- Define canonical source paths as `Assets/...` package-relative paths.
- Normalize separators and accept legacy `foo.png` when possible.
- Add `GuidForAbsolutePath` or a safer source lookup API.
- Update Project Browser, Scene View, and Inspector drag/drop to use the canonical lookup.

### Phase 2. Catalog Save/Load

- Add `AssetDatabase::SaveCatalog(path)`, `LoadCatalog(path, packageRoot)`, and `Clear()`.
- `AbsoluteSourcePath(guid)` should resolve through catalog records at runtime.
- Runtime should load catalog before scene load or at least before `world.ResolveAssets()`.

### Phase 3. AssetResolver

- Add `AssetResolver` for `guid + legacyPath + expectedType`.
- Migrate `SpriteRenderer`, `Material::mainTexture`, and `AudioSource` first.
- Keep `texturePath` fallback for legacy scenes, but log migration warnings in development mode.
- Treat missing asset as structured state and standard placeholder, not ad hoc fallback logic.

### Phase 4. Build Pipeline

- Build should refresh `AssetDatabase` before packaging.
- Emit `asset_catalog.json` into staging root.
- Copy the placeholder runtime resource.
- Keep copying full `Assets` initially; dependency-closure copying can come later after catalog tests are stable.

### Phase 5. Package Validation and Smoke

- `PackageLayout::Validate` should require catalog and placeholder resource.
- Smoke fixtures should include GUID-only scenes.
- Runtime smoke should report catalog loaded, resolved asset count, and missing asset count.

## Subagent Plan

| Worker | Owns | Deliverables | Must avoid |
|---|---|---|---|
| A. Asset DB/Catalog | `AssetDatabase.*`, `AssetMeta.*`, importers | Catalog schema, save/load, path normalization tests | Component migrations |
| B. Runtime Resolver | new `AssetResolver.*`, `SpriteRenderer.*`, `Material.*`, `AudioSource.*` | GUID-only runtime resolve and missing placeholder policy | Asset DB internals |
| C. Build/Package | `GameBuilder.*`, `PackageLayout.*`, `PathConstants.h`, smoke CMake | Catalog/resource copy and validation | Component behavior |
| D. Editor Migration | `ProjectBrowserWindow.*`, `SceneViewWindow.*`, `InspectorWindow.*`, reference scan | Stable GUID drag/drop and path cleanup | Catalog persistence internals |
| E. Tests | asset catalog, sprite runtime, package smoke tests | Regression coverage | Production logic beyond fixtures |

Worker A defines API and schema first. Worker B and C should consume that API rather than inventing parallel JSON readers.

## Tests

- `test_asset_catalog`: scan, save, clear, load, and resolve GUID to package-root path.
- `test_asset_database_path_normalization`: `foo.png`, `Assets/foo.png`, and backslash paths resolve consistently.
- `test_sprite_renderer_guid_runtime`: `textureGuid` without `texturePath` resolves through catalog.
- `test_missing_texture_packaged`: placeholder loads and package validation fails if absent.
- `smoke_end_to_end`: GUID-only scene builds and runs, runtime report includes catalog statistics.

## Done Criteria

- Runtime packages can load sprite textures from `textureGuid` only.
- Runtime does not create `.meta` files or run importers.
- Editor drag/drop, save, reload, play mode, and package runtime preserve the same GUID.
- Missing texture placeholder is packaged and validated.
- `SpriteRenderer`, `Material` main texture, and `AudioSource` work without path fallback for new content.

## Risks

- Changing canonical paths may break old tests and saved scenes without migration.
- `TextureManager` caches by path string, so GUID and path fallback can cause duplicate loads.
- Catalog staleness must be caught before packaging.
- Windows separators and case sensitivity can create GUID lookup bugs.
