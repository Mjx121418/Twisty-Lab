# GitHub Pages deployment

Twisty Lab is a static site. GitHub Pages serves the JavaScript, WebAssembly, meshes, and puzzle data; simulation and rendering run in the visitor's browser. Publishing needs only the WASM build; the native CLI is used by the full development checks.

The workflow in [`.github/workflows/pages.yml`](../.github/workflows/pages.yml) supports manual runs and a daily schedule at 00:13 UTC. Push source from macOS; a push alone does not start deployment. Scheduled runs publish the default branch and may start later than the scheduled time.

## First upload from macOS

1. Create an empty repository in your GitHub account, for example `twisty-lab`. Use a public repository if you are using GitHub Free. Leave the README, license, and `.gitignore` initialization options unchecked because the local repository already contains files and commits.
2. In a macOS terminal, open the project folder that contains this repository's `.git` directory. If `/workspace` is a bind mount of that folder, its commits are already available on macOS. If you transfer the folder, include `.git` to preserve the existing history.
3. Replace `YOUR_USERNAME` and `YOUR_REPOSITORY` below and run:

   ```sh
   git status
   git remote add origin https://github.com/YOUR_USERNAME/YOUR_REPOSITORY.git
   git push -u origin main
   ```

   Authenticate using your usual GitHub setup. If you use SSH, copy the SSH remote URL from GitHub instead. The workspace currently has no remote; if you have already added one, inspect `git remote -v` and skip the `remote add` command. You only need Git on macOS for uploading; GitHub installs the build tools.

4. Ensure `main` is the default branch. In **Settings → Pages → Build and deployment**, select **GitHub Actions** as the source. Enable repository Actions if they are disabled.
5. Open **Actions → Deploy GitHub Pages → Run workflow**, choose `main`, and click **Run workflow**. The workflow must be present on the default branch for the manual button to appear.
6. Wait for both the build and deployment jobs to succeed. Open the site URL shown by the deployment or **Settings → Pages**.

For a project repository named `twisty-lab`, the usual URLs are:

- Simulator: `https://YOUR_USERNAME.github.io/twisty-lab/`
- Independent Canvas renderer/controller: `https://YOUR_USERNAME.github.io/twisty-lab/kernel/examples/canvas/`

A repository named `YOUR_USERNAME.github.io` serves at `/`. The workflow reads the actual Pages base path, so the build also handles that root site or a configured custom domain without hardcoding a repository name. Custom-domain DNS setup is separate.

## Publish later updates

Commit the changes you want to upload, then push from macOS:

```sh
git push origin main
```

To update the live site immediately, manually run **Deploy GitHub Pages** on `main` again; otherwise, wait for the next scheduled run. Each run builds the checked-out commit. The deployment replaces the whole static site, including the WASM SDK and example data. To republish an earlier version, revert the relevant source commits, push, and run the workflow again.

The build uses Node 24.21.0, Emscripten 6.0.11, and the checked-in npm lockfile. `npm run check:puzzles -- --files-only` first verifies the checked-in manifest before build tools are installed. `npm run build:web` checks freshness again, compiles WASM, validates all 13 prebuilt packages and their 27 realizations with that engine, packages the independent SDK, checks TypeScript, and builds `dist/`. Validation checks JSON schemas, package/source/catalog agreement, realization and fixture semantic pins, and construction of every declared realization. `npm run test:publish` then runs two browser smoke checks: every preset must load, move, and undo in both the simulator and the standalone Canvas client, without missing assets or uncaught browser errors. Failures prevent deployment. Compilation uses one job, browser checks use one worker, and heavy workloads run sequentially. The workflow installs its tools directly and does not build or modify the development Dockerfile.

Publishing skips native compilation, package regeneration audits, native/WASM parity tests, and the detailed browser regression suite. Run `npm run check` during development before publishing changes to rules, geometry, or generated packages. That command explicitly includes the native build and full reproducibility audit; the shorter publishing check verifies that the built site and SDK work at the Pages URL.

## Updating puzzle data

App code and documentation changes can use the committed puzzle packages directly. Changes to recorded authoring inputs or runtime artifacts cause publishing to fail with the affected path and a rebuild instruction. In the development environment, run:

```sh
npm run build:puzzles
npm run check:packages
```

Review and commit the generated data and `packages/build-manifest.json` together. Use `npm run build:puzzles -- --update-fixtures` only when deliberately accepting new semantic identities. The action never regenerates puzzles or updates their hashes. The WASM engine continues to build from source, independently of the puzzle authoring process.

## Check the repository path locally

In the development container, build and test with the same base path. Replace `twisty-lab` with your repository name and keep the leading and trailing slashes:

```sh
SITE_BASE_PATH=/twisty-lab/ npm run build:web
env -u DISPLAY -u WAYLAND_DISPLAY -u PWDEBUG SITE_BASE_PATH=/twisty-lab/ npm run test:publish
SITE_BASE_PATH=/twisty-lab/ npm run preview
```

Open `http://localhost:4173/twisty-lab/` or `http://localhost:4173/twisty-lab/kernel/examples/canvas/`. Stop preview with Ctrl+C. Build and preview must use the same path; after this check, run `npm run build:web` without `SITE_BASE_PATH` to restore the normal `/` build. Generated build output stays ignored by Git and is uploaded as an Actions artifact rather than committed.

If deployment fails, open the failed step in its Actions run. A missing Pages site usually means **Settings → Pages** has not been switched to **GitHub Actions**. Missing JS or WASM assets usually mean the build base path and hosting path differ; rerun the workflow after any Pages URL or custom-domain change. Browser-local saved data is not shared between different site origins.

References: [GitHub's Pages custom-workflow guide](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages), [manually running a workflow](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow), and [Vite's GitHub Pages deployment guide](https://vite.dev/guide/static-deploy.html#github-pages).
