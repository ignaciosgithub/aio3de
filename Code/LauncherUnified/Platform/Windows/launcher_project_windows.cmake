#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

# Validates that a resource file is a real binary of the expected kind before
# handing it to rc.exe (which fails the whole launcher link with RC2175 on bad
# input). The most common bad input is a Git LFS pointer text file from a
# checkout without 'git lfs pull' - detect that case and say so.
#   ly_validate_launcher_resource(<out_var> <path> <ICO|BMP>)
# sets <out_var> to <path> when valid, or to "" (with a warning) when not.
function(ly_validate_launcher_resource out_var resource_file expected_kind)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT EXISTS ${resource_file})
        return()
    endif()
    file(READ ${resource_file} file_magic LIMIT 8 HEX)
    if(expected_kind STREQUAL "ICO")
        set(valid_magic "00000100")
        string(SUBSTRING "${file_magic}" 0 8 file_magic_prefix)
    else() # BMP
        set(valid_magic "424d")
        string(SUBSTRING "${file_magic}" 0 4 file_magic_prefix)
    endif()
    if(file_magic_prefix STREQUAL valid_magic)
        set(${out_var} ${resource_file} PARENT_SCOPE)
        return()
    endif()
    # "version https://git-lfs..." begins with hex 76657273696f6e20
    string(SUBSTRING "${file_magic}" 0 16 lfs_probe)
    if(lfs_probe STREQUAL "76657273696f6e20")
        message(WARNING
            "${resource_file} is a Git LFS pointer, not a real ${expected_kind} file - "
            "run 'git lfs pull' in the repository that provides it. "
            "Building the launcher without it for now.")
    else()
        message(WARNING
            "${resource_file} is not a valid ${expected_kind} file - "
            "replace it with a real binary ${expected_kind}. "
            "Building the launcher without it for now.")
    endif()
endfunction()

# candidate icon locations, first valid one wins
set(ICON_FILE "")
foreach(candidate
    ${project_real_path}/Gem/Resources/GameSDK.ico
    ${project_real_path}/Resources/GameSDK.ico
    ${CMAKE_CURRENT_LIST_DIR}/../../Resources/GameSDK.ico)
    ly_validate_launcher_resource(ICON_FILE ${candidate} ICO)
    if(ICON_FILE)
        break()
    endif()
endforeach()

# candidate splash locations, first valid one wins
set(SPLASH_FILE "")
foreach(candidate
    ${project_real_path}/Resources/Splash.bmp
    ${project_real_path}/Gem/Resources/Splash.bmp
    ${project_real_path}/Resources/LegacyLogoLauncher.bmp
    ${project_real_path}/Gem/Resources/LegacyLogoLauncher.bmp)
    ly_validate_launcher_resource(SPLASH_FILE ${candidate} BMP)
    if(SPLASH_FILE)
        break()
    endif()
endforeach()

# emit only the resource statements whose files are actually usable, so one
# bad/missing file never breaks the launcher build
set(RC_ICON_STATEMENT "")
if(ICON_FILE)
    set(RC_ICON_STATEMENT "IDI_ICON1 ICON DISCARDABLE \"${ICON_FILE}\"")
endif()
set(RC_SPLASH_STATEMENT "")
if(SPLASH_FILE)
    set(RC_SPLASH_STATEMENT "IDB_SPLASH1 BITMAP DISCARDABLE \"${SPLASH_FILE}\"")
endif()

if(RC_ICON_STATEMENT OR RC_SPLASH_STATEMENT)
    set(target_file ${CMAKE_CURRENT_BINARY_DIR}/${project_name}.GameLauncher.rc)
    configure_file(${CMAKE_CURRENT_LIST_DIR}/Launcher.rc.in
        ${target_file}
        @ONLY
    )
    set(LY_FILES ${target_file})
endif()
