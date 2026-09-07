# Maintaining documentation

## Source of truth

| Change | Check and update |
|---|---|
| Facade/bindings | Public headers, Cython wrapper/pxd, package exports, both language references |
| JSON behavior | Native parser **and executor/compiler**, Python validator, shader/layout examples, pipeline reference |
| Build/package | CMake options, vcpkg manifest, pyproject, CI, BUILDING.md |
| Example/assets | Actual target/script names, source working directories, shader compilation, bundled/optional assets |
| Engine lifecycle | ApplicationBase and facade CallbackApp ordering; callback documentation |

Describe implemented behavior, including native-only surfaces and known limitations. A field declaration alone does not prove a feature works. Avoid unsupported speed, determinism, platform, and feature-parity claims.

## Checks

From the repository root:

```sh
python tools/generate_api_docs.py
python tools/generate_api_docs.py --check
python tools/check_docs.py
python -m unittest discover -s tests/python -v
```

The API generator updates only marked inventories in five C++ and five Python references. Maintain the surrounding explanatory prose by hand. It is tailored to this repository's header/wrapper style, so changes to those declaration styles may require extractor changes.

The documentation checker validates active local Markdown paths/anchors and Python/JSON fence syntax. It does not fetch external URLs or execute snippets. Complete pipelines need the existing validator and native compilation/runtime checks; excerpts need context and must be labeled as fragments. Keep generated member signatures separate from runnable code examples.

For quickstart changes, build FacadeTest and launch the Python starter from the documented working directory. Check required assets and shaders. For renderer contracts, exercise the matching GPU example; headless tests are not a rendering test. Record checks that could not run.

## Organization

Keep build details in BUILDING.md, feature overview in README, task guidance under guides, exact configuration in reference, and public interfaces under api. Link instead of duplicating large examples. Keep authored JSON/shaders as the runnable source of truth.

Plans, dated reviews, and old guides retain historical banners and are indexed in [archive](archive.md). Do not link readers to them as current instructions. Preserve branding, dedication, and asset provenance when editing navigation.
