#!/usr/bin/env bash
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
# SPDX-License-Identifier: Apache-2.0 OR MIT
SOURCE="${BASH_SOURCE[0]}"
while [[ -h "$SOURCE" ]]; do
    DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
    SOURCE="$(readlink "$SOURCE")"
    [[ "$SOURCE" != /* ]] && SOURCE="$DIR/$SOURCE"
done
DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
O3DE_PROJECT_PATH="$DIR"
O3DE_PROJECT_SEEDLIST="${O3DE_PROJECT_PATH}/AssetBundling/SeedLists/Circuit.seed"
O3DE_PATH="${EnginePath}"

if [[ ! -f "${O3DE_PATH}/scripts/o3de.sh" ]]; then
    echo "Invalid engine path: $O3DE_PATH" >&2
    exit 1
fi
if [[ "${1-}" == "-h" ]]; then
    echo "Usage: $0 [EXPORT_PATH]"
    exit 0
fi
if [[ -z "${1-}" ]]; then
    OUTPUT_PATH="${O3DE_PROJECT_PATH}/build/export"
else
    OUTPUT_PATH="$1"
fi

"${O3DE_PATH}/scripts/o3de.sh" export-project -es ExportScripts/export_source_built_project.py --project-path "${O3DE_PROJECT_PATH}" --log-level INFO --seedlist "${O3DE_PROJECT_SEEDLIST}" -out "${OUTPUT_PATH}"
