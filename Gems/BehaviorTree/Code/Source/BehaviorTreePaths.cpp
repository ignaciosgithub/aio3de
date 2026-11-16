/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "BehaviorTreePaths.h"

#include <AzCore/std/utility/move.h>

namespace BehaviorTree
{
    AZStd::vector<AZStd::string> GetTreePathCandidates(const AZStd::string& treeFile)
    {
        AZStd::vector<AZStd::string> candidates;
        if (treeFile.empty())
        {
            return candidates;
        }

        if (treeFile[0] == '/' || treeFile[0] == '@' || treeFile.find(':') != AZStd::string::npos)
        {
            candidates.push_back(treeFile);
            return candidates;
        }

        AZStd::string relativePath = treeFile;
        for (char& character : relativePath)
        {
            if (character == '\\')
            {
                character = '/';
            }
        }
        if (relativePath.size() >= 2 && relativePath[0] == '.' && relativePath[1] == '/')
        {
            relativePath.erase(0, 2);
        }

        AZStd::string lowerCasePath = relativePath;
        for (char& character : lowerCasePath)
        {
            if (character >= 'A' && character <= 'Z')
            {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }

        candidates.push_back(AZStd::string("@projectroot@/") + relativePath);
        candidates.push_back(AZStd::string("@products@/") + lowerCasePath);

        AZStd::string casePreservingProductPath = AZStd::string("@products@/") + relativePath;
        if (casePreservingProductPath != candidates.back())
        {
            candidates.push_back(AZStd::move(casePreservingProductPath));
        }
        return candidates;
    }
} // namespace BehaviorTree
