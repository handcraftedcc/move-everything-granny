# Granny Release Workflow (Tag-Based)

This project now uses a GitHub tag-based release flow.
Do not publish releases from local `gh release create`.

## Goal

Create a release by pushing a `v*` tag.
GitHub Actions builds the tarball and publishes the release automatically.

## One-Time Setup (Already Done in Granny)

1. Ensure `.github/workflows/release.yml` exists.
2. Ensure workflow trigger includes `on.push.tags: "v*"`.
3. Ensure workflow builds `dist/granny-grain-module.tar.gz`.
4. Ensure workflow creates release via `softprops/action-gh-release`.

## Per-Release Checklist

1. Sync local `main`:
```bash
git checkout main
git pull --ff-only origin main
```

2. Confirm clean working tree:
```bash
git status --short --branch
```

3. Bump versions:
Update `src/module.json` `"version"` (example: `0.1.5`).
Update `release.json` `"version"` to same value.
Update `release.json` `"download_url"` to matching tag URL (example: `.../v0.1.5/...`).

4. Commit version bump:
```bash
git add src/module.json release.json
git commit -m "release(granny): bump version to v0.1.5"
git push origin main
```

5. Create and push tag:
```bash
git tag -a v0.1.5 -m "Release v0.1.5"
git push origin v0.1.5
```

6. Watch GitHub Actions:
Open the `Release` workflow run for the new tag.
Wait for all jobs to pass.

7. Validate GitHub release:
Confirm release exists for `v0.1.5`.
Confirm asset `granny-grain-module.tar.gz` is attached.
Download once and confirm contents:
```bash
tar -tzf granny-grain-module.tar.gz
```

## Failure Handling

1. If the workflow fails before release creation:
Fix on `main` without changing version.
Keep the same release version/tag and rerun by recreating the tag:
```bash
git tag -d v0.1.5
git push origin :refs/tags/v0.1.5
git tag -a v0.1.5 -m "Release v0.1.5"
git push origin v0.1.5
```

2. If a bad release is already published:
Leave old tag/release for traceability.
Ship a new patch release with fixes.

## Notes for Other Modules

1. Reuse this flow unchanged.
2. Only adjust artifact names/paths in the workflow and checklist.
3. Keep version in module metadata and release manifest in sync.
