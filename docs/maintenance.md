# Maintaining documentation

## Preview and build the site

The site uses [MkDocs](https://www.mkdocs.org/) with its built-in Read the Docs theme, plus Doxygen for the native API reference. It needs Python 3.10 or newer and does not require a Vulkan SDK, a GPU, or a compiled engine.

Install **Doxygen 1.9.8 or newer** and make sure `doxygen` is on `PATH`. On Ubuntu/Debian:

```sh
sudo apt-get install doxygen
```

On Windows, install [Doxygen](https://www.doxygen.nl/download.html), adding its executable directory to `PATH`. Verify `doxygen --version` in the shell used for previewing. Inheritance diagrams use Doxygen's built-in renderer. Then run from the repository root:

```sh
python -m venv .venv-docs
```

Activate the environment on Windows PowerShell:

```powershell
.venv-docs/Scripts/Activate.ps1
```

On Linux/macOS:

```sh
source .venv-docs/bin/activate
```

Then install and preview:

```sh
python -m pip install -r requirements-docs.txt
python -m mkdocs serve
```

Open [the local preview](http://127.0.0.1:8000/Shoonyakasha/). MkDocs watches documentation and header changes and regenerates the C++ reference on each build. Restart it after changing the build hook. Run one preview/build at a time, because Doxygen writes to a shared generated directory. To produce the same static output as CI:

```sh
python -m mkdocs build --strict
python tools/check_docs_site.py
```

The generated `site/` directory is ignored by Git. The site includes search, nested navigation, syntax highlighting, and links to edit the original Markdown on GitHub.

## Publish on GitHub Pages

The [documentation workflow](../.github/workflows/docs.yml) checks and builds the site for pull requests. Pushes to `master` also upload the result and deploy it to GitHub Pages. The engine build runs separately.

For the initial repository setup:

1. Commit and push the documentation configuration and workflow to `master`.
2. In the GitHub repository, open **Settings → Pages → Build and deployment** and select **GitHub Actions** as the source.
3. Open **Actions → Documentation → Run workflow** on `master` if the first deployment ran before Pages was enabled.
4. Check that both the build and publish jobs succeed. The deployment reports the live URL.

The configured project URL is [maximilian-winter.github.io/Shoonyakasha](https://maximilian-winter.github.io/Shoonyakasha/). It becomes live after a successful deployment; adding these files alone does not enable Pages.

The deployment job uses the `github-pages` environment and GitHub's built-in token. It requests Pages and OIDC permissions only for deployment; pull requests run the build without deployment permissions. Existing repository or environment protection rules still apply. See GitHub's [custom workflow documentation](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).

If the repository, default branch, or hosting URL changes, update `site_url`, `repo_url`, `edit_uri`, and `extra.source_branch` in [mkdocs.yml](../mkdocs.yml), plus the branch conditions in the documentation workflow.

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
python -m mkdocs build --strict
python tools/check_docs_site.py
python -m unittest discover -s tests/python -v
```

The lightweight API inventory generator updates only marked inventories in six C++ and six Python contract pages. Maintain the surrounding explanatory prose by hand. It is tailored to this repository's header/wrapper style, so changes to those declaration styles may require extractor changes. The full native symbol reference is built separately with Doxygen.

The documentation checker validates active local Markdown paths/anchors and Python/JSON fence syntax. It does not fetch external URLs or execute snippets. Complete pipelines need the existing validator and native compilation/runtime checks; excerpts need context and must be labeled as fragments. Keep generated member signatures separate from runnable code examples.

For quickstart changes, build FacadeTest and launch the Python starter from the documented working directory. Check required assets and shaders. For renderer contracts, exercise the matching GPU example; headless tests are not a rendering test. Record checks that could not run.

## Technical C++ API

[Doxyfile](../Doxyfile) defines the native API extraction. Each MkDocs build invokes [the native reference builder](../tools/build_native_api.py), which generates HTML and XML under the ignored `build/docs-api/` directory. The HTML is included in the site at `api/native/`; XML is available locally for tooling. Doxygen runs with documentation warnings treated as errors; details are recorded in `build/docs-api/warnings.log`.

To generate only the native reference:

```sh
python tools/build_native_api.py
```

The normal `python -m mkdocs build --strict` command includes this step automatically. CI installs Doxygen and publishes the native reference in the same Pages artifact as the guides. No separately deployed API site is required.

The builder derives a navigation layout from the installed Doxygen version and adds a **Documentation** link back to MkDocs on API pages. Symbol search runs entirely in the browser. The generated-site checker validates native HTML links, source anchors, and diagram assets alongside the guide pages.

Write technical documentation beside declarations using `///` or `/** ... */`:

```cpp
/**
 * @brief Load a resource from a path relative to the asset root.
 * @param path Resource path; must not be empty.
 * @return True when the resource was loaded successfully.
 * @throws std::invalid_argument If path is empty.
 */
```

This is a comment-format example, not a description of an existing loader. Document actual behavior checked against the implementation. Include units, defaults, ownership, required initialization phase, error behavior, and relevant `@see` links. Use backticks around literal markup such as `<font=name>` so it is not interpreted as HTML.

The generated reference includes undocumented public/protected declarations to keep the catalog complete; it does not claim every member has explanatory prose. It excludes private members, implementation translation units, dependencies, examples, and testing-only APIs. Preprocessing defines `NDEBUG`, so build-dependent defaults reflect release configuration. Extend comments incrementally when changing APIs.

The [API overview](api/index.md) contains canonical published links to the symbol indexes. The build hook converts links pointing to generated files into relative local links, preserving the repository subpath and local preview. MkDocs search covers authored pages; Doxygen's own search indexes C++ symbols.

## Organization

Use one purpose per page:

| Kind | Location | Contents |
|---|---|---|
| Learning sequence | `getting-started/` | Prerequisites, ordered steps, a visible success checkpoint, and the next lesson |
| Task guide | `guides/` | A concrete goal, required assets/setup, steps, and relevant limitations |
| Explanation | `architecture/` | Design intent, ownership, lifecycle, and data flow |
| Reference | `api/`, `reference/` | Signatures, configuration fields, defaults, units, and contracts |
| Example catalog | `examples/` | Runnable sources, working directories, commands, and expected output |

Keep build details in BUILDING.md and feature overview in README. Link instead of duplicating large examples. Keep authored JSON/shaders as the runnable source of truth.

When adding a page, add it to the explicit `nav` in [mkdocs.yml](../mkdocs.yml) and link it from a relevant guide or index. A strict build rejects unlisted pages, missing navigation targets, and broken local links/anchors. Prefer ordinary relative `.md` links so GitHub and the site both work.

The [site hook](../tools/docs_site.py) publishes BUILDING.md, examples/README.md, and assets/README.md as pages from their original files; edit those sources rather than copying them into `docs/`. It also includes the root logo and rewrites links to repository source files and historical material into GitHub URLs. Code fences and inline code remain unchanged. The repository checker catches missing source paths; the strict site build catches missing hosted pages and anchors. The [generated-site checker](../tools/check_docs_site.py) also validates links and images written as raw HTML. These checks do not verify remote URLs.

Plans, dated reviews, and old guides retain historical banners and are indexed in [archive](archive.md). Do not link readers to them as current instructions. Preserve branding, dedication, and asset provenance when editing navigation.

Historical page bodies are excluded from the published site and its search index; the archive links to their original GitHub paths. The site documents the current `master` revision. Release-specific documentation and a version selector are not configured.
