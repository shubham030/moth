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
4. **Publish `moth` first, then `mothc`** (mothc's README links the moth
   package): `dart pub publish` in each directory. This claims both names.
5. **Tag**: `git tag -a v0.1.0 -m "v0.1.0"` && `git push origin v0.1.0`.
   Create the GitHub release from the tag with the notes below.
6. **Repo public** (first release only): GitHub → Settings → change
   visibility. The docs site and README links assume the repo is readable.
7. **Sanity loop, as a stranger**: `dart pub global activate mothc`,
   `mothc create /tmp/hello`, open it in an editor (no red squiggles),
   `mothc /tmp/hello/app.dart`.

Publishing uses the pub.dev account of whoever runs it; both packages
should end up under the same publisher.
