#!/bin/sh

tag=$(git describe --tags --contains 2>/dev/null | sed -r 's/\^.$//' || true)
git_version=$(git describe --tags --always)

if [ -n "${tag}" ]; then
    # If tag is of format "version-xxx", retain only "xxx"
    echo ${tag#version-}
else
    echo ${git_version#version-}
fi
