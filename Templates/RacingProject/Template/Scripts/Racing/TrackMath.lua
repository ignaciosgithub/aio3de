--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local TrackData = require("Scripts.Racing.TrackData")

local TrackMath = {}
local points = TrackData.points
local count = #points

local function wrap(index)
    return ((index - 1) % count) + 1
end

local function vectorAt(index)
    local point = points[wrap(index)]
    return Vector3(point.x, point.y, point.z)
end

local function tangentAtProgress(progress)
    local cursor = progress % count
    local index = math.floor(cursor) + 1
    local fraction = cursor - math.floor(cursor)
    local first = vectorAt(index + 1) - vectorAt(index)
    local second = vectorAt(index + 2) - vectorAt(index + 1)
    local tangent = first * (1.0 - fraction) + second * fraction
    local length = math.sqrt(tangent.x * tangent.x + tangent.y * tangent.y)
    if length <= 0.0001 then
        return Vector3(0.0, 1.0, 0.0)
    end
    return tangent / length
end

function TrackMath.nearestIndex(position, hintIndex)
    local startIndex = hintIndex and wrap(math.floor(hintIndex)) or 1
    local firstOffset = hintIndex and -20 or 0
    local lastOffset = hintIndex and 20 or (count - 1)
    local bestIndex = 1
    local bestDistance = math.huge
    for offset = firstOffset, lastOffset do
        local index = hintIndex and wrap(startIndex + offset) or offset + 1
        local point = points[index]
        local dx = position.x - point.x
        local dy = position.y - point.y
        local distance = dx * dx + dy * dy
        if distance < bestDistance then
            bestDistance = distance
            bestIndex = index
        end
    end
    return bestIndex
end

function TrackMath.progress(position, index)
    index = wrap(index)
    local first = vectorAt(index)
    local second = vectorAt(index + 1)
    local segment = second - first
    local segmentLengthSquared = segment.x * segment.x + segment.y * segment.y
    if segmentLengthSquared <= 0.0001 then
        return index - 1
    end
    local offset = position - first
    local fraction = (offset.x * segment.x + offset.y * segment.y) / segmentLengthSquared
    fraction = math.max(0.0, math.min(1.0, fraction))
    return index - 1 + fraction
end

function TrackMath.tangent(index)
    return tangentAtProgress(wrap(index) - 1)
end

function TrackMath.positionAtProgress(progress)
    local cursor = progress % count
    local index = math.floor(cursor) + 1
    local fraction = cursor - math.floor(cursor)
    return vectorAt(index) * (1.0 - fraction) + vectorAt(index + 1) * fraction
end

function TrackMath.pointAhead(progress, distance)
    local cursor = progress
    local remaining = distance
    while remaining > 0.0 do
        local index = math.floor(cursor % count) + 1
        local fraction = cursor % 1.0
        local first = vectorAt(index)
        local second = vectorAt(index + 1)
        local segmentLength = (second - first):GetLength()
        if segmentLength <= 0.0001 then
            cursor = cursor + 0.01
        else
            local available = segmentLength * (1.0 - fraction)
            local step = math.min(remaining, available)
            cursor = cursor + step / segmentLength
            remaining = remaining - step
        end
    end
    return TrackMath.positionAtProgress(cursor), tangentAtProgress(cursor), cursor
end

function TrackMath.lateralOffset(position, index)
    local progress = TrackMath.progress(position, index)
    local center = TrackMath.positionAtProgress(progress)
    local tangent = tangentAtProgress(progress)
    local side = Vector3(-tangent.y, tangent.x, 0.0)
    local offset = position - center
    return offset.x * side.x + offset.y * side.y
end

return TrackMath
