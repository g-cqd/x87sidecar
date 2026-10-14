# Keeping this fork current

`main` is the single working branch for this fork and the default branch. It contains the merged NFSMW patches and the consolidated history from the previous `development` and `master` branches. The transient branches have been removed; their commits remain reachable from `main`.

The `upstream` remote tracks athei/x87sidecar independently of the fork's working branch.

## Clone once

```sh
git clone git@github.com:g-cqd/x87sidecar.git
cd x87sidecar
git remote add upstream https://github.com/athei/x87sidecar.git
git switch main
```

## Integrate an upstream update

Start with a clean working tree, then merge without rewriting the fork's history:

```sh
git fetch upstream --tags
git switch main
git merge --no-commit --no-ff upstream/master
```

Resolve conflicts and run the repository's build and test gates, including the NFSMW regressions, before committing the merge. `git merge --abort` returns an interrupted merge to its starting point. Keep installed game runtimes separate from validation builds.

Push verified changes to `origin/main` with a normal push. The `main` branch is both the default and the single working branch; integrate upstream updates here directly without force-pushing or creating replacement working branches.
