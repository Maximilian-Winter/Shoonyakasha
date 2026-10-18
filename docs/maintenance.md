# Maintaining documentation

## Preview and build the site

The site uses [MkDocs](https://www.mkdocs.org/) with its built-in Read the Docs theme. It needs Python 3.10 or newer and does not require a Vulkan SDK, a GPU, or a compiled engine. Run from the repository root:

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

Open [the local preview](http://127.0.0.1:8000/Shoonyakasha/). MkDocs watches documentation changes. Restart it after changing the build hook. To produce the same static output as CI:

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

The API generator updates only marked inventories in five C++ and five Python references. Maintain the surrounding explanatory prose by hand. It is tailored to this repository's header/wrapper style, so changes to those declaration styles may require extractor changes.

The documentation checker validates active local Markdown paths/anchors and Python/JSON fence syntax. It does not fetch external URLs or execute snippets. Complete pipelines need the existing validator and native compilation/runtime checks; excerpts need context and must be labeled as fragments. Keep generated member signatures separate from runnable code examples.

For quickstart changes, build FacadeTest and launch the Python starter from the documented working directory. Check required assets and shaders. For renderer contracts, exercise the matching GPU example; headless tests are not a rendering test. Record checks that could not run.

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
