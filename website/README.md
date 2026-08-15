# moth website

The Docusaurus site behind [the moth docs](https://shubham030.github.io/moth/).

The one surprising thing about this setup: **the docs source lives in
`../docs`, not in `website/docs`** — the site serves the repo's `docs/`
directory directly (`docusaurus.config.ts`, `path: '../docs'`), so a doc fix
in the repo is a site fix with no copying. `PERF_REVIEW.md` is excluded — it
is the internal render-review checklist, not a doc.

## Working on it

```
npm install
npm start        # local dev server with hot reload
npm run build    # what CI runs; broken links and anchors fail the build
```

CI builds the site on every PR (the `docs` job in
`.github/workflows/ci.yml`), so a stale link fails before it merges.

## Publishing

There is no automated deploy yet. To publish by hand to GitHub Pages:

```
GIT_USER=<your-github-username> npm run deploy
```

which builds and pushes to the `gh-pages` branch. The served URL and base
path are set in `docusaurus.config.ts` (`url` + `baseUrl`).
