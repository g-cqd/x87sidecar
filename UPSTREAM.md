# Keeping this fork current

`master` is the single working branch for this fork. It contains the NFSMW patches and the other development branch histories. Historical branches remain available as references; new work belongs on `master`.

The `upstream` remote tracks athei/x87sidecar independently of the fork's working branch.

## Clone once

```sh
git clone git@github.com:g-cqd/x87sidecar.git
cd x87sidecar
git remote add upstream https://github.com/athei/x87sidecar.git
git switch master
```

## Integrate an upstream update

Start with a clean working tree, then merge without rewriting the fork's history:

```sh
git fetch upstream --tags
git switch master
git merge --no-commit --no-ff upstream/master
```

Resolve conflicts and run the repository's build and test gates, including the NFSMW regressions, before committing the merge. `git merge --abort` returns an interrupted merge to its starting point. Keep installed game runtimes separate from validation builds.

Push verified changes to `origin/master` with a normal push. Do not force-push or delete the historical branches as part of an update.
