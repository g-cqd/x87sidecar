# Keeping this fork current

`development` is the single working branch for this fork, alongside the default `master` branch. It contains the merged NFSMW patches and the previous development branch histories. The transient branches have been removed; their commits remain reachable from `development`.

The `upstream` remote tracks athei/x87sidecar independently of the fork's working branch.

## Clone once

```sh
git clone git@github.com:g-cqd/x87sidecar.git
cd x87sidecar
git remote add upstream https://github.com/athei/x87sidecar.git
git switch development
```

## Integrate an upstream update

Start with a clean working tree, then merge without rewriting the fork's history:

```sh
git fetch upstream --tags
git switch development
git merge --no-commit --no-ff upstream/master
```

Resolve conflicts and run the repository's build and test gates, including the NFSMW regressions, before committing the merge. `git merge --abort` returns an interrupted merge to its starting point. Keep installed game runtimes separate from validation builds.

Push verified changes to `origin/development` with a normal push. Keep only the default branch and `development`; integrate upstream updates here without force-pushing or creating replacement working branches.
