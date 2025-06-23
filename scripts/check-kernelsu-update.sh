#!/bin/bash

git config --local user.email "41898282+github-actions[bot]@users.noreply.github.com"
git config --local user.name "github-actions[bot]"
git submodule update --init
pushd KernelSU-Next
LOCAL_LATEST=$(git describe --tags --abbrev=0)
git pull origin next
REMOTE_LATEST=$(git describe --tags --abbrev=0)
if [ $LOCAL_LATEST = $REMOTE_LATEST ]; then
echo "No changes: $LOCAL_LATEST is latest"
exit 0;
fi
git checkout $REMOTE_LATEST
popd
git add KernelSU-Next
git commit -m "KernelSU-Next $REMOTE_LATEST" -m "$LOCAL_LATEST -> $REMOTE_LATEST"
git push
