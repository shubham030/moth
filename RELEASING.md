# Releasing moth

The order matters: pub.dev names are first-come, and the scaffold's hosted
fallback assumes `moth` on pub.dev is ours.

## v0.1 (and the shape of every release after)

1. **Green suite on main** — `make test`, plus the CI runs on the release
   commit.
2. **Versions agree** — `packages/moth/pubspec.yaml`,
   `tools/mothc/pubspec.yaml`, and both CHANGELOGs name the same version.
3. **Dry-runs pass** — `dart pub publish --dry-run` in `packages/moth`
   and in `tools/mothc`; the only acceptable warning is uncommitted state
   before the release commit lands.
4. **Publish `moth` first, then `mothc`.** The order is load-bearing: the
   moment `mothc` is installable, its scaffold writes `moth: ^0.1.0` into
   every project it creates — that name must already be ours, or those
   projects resolve to whoever claimed it. `dart pub publish` in each
   directory.
5. **Tag**: `git tag -a v0.1.0 -m "v0.1.0"` && `git push origin v0.1.0`.
   Create the GitHub release from the tag with the notes below.
6. **Repo public** (first release only): GitHub → Settings → change
   visibility. The docs site and README links assume the repo is readable.
7. **Sanity loop, as a stranger**: `dart pub global activate mothc`,
   `moth create /tmp/hello`, open it in an editor (no red squiggles),
   then `moth run` in the project — the loop the README promises, under
   the `moth` name specifically, since the alias only exists installed.

Publishing uses the pub.dev account of whoever runs it; both packages
should end up under the same publisher.
