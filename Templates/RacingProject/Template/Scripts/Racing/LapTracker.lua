--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local TrackData = require("Scripts.Racing.TrackData")
local TrackMath = require("Scripts.Racing.TrackMath")

local LapTracker = {
    Properties = {
        CarIndex = { default = 0.0 },
        Laps = { default = 3.0 },
        RaceManager = { default = EntityId() },
    },
}

function LapTracker:OnActivate()
    self.raceState = 0.0
    self.started = false
    self.finished = false
    self.currentLap = 1
    self.completedLaps = 0
    self.raceTime = 0.0
    self.lapTime = 0.0
    self.bestLap = math.huge
    self.visitedQuarter2 = false
    self.visitedQuarter3 = false
    self.lapArmed = false
    self.lastIndex = nil
    self.lastProgress = 0.0
    self.callbackTables = {}

    self.raceCallback = {
        OnEventBegin = function(_, value)
            if value == 1.0 and not self.started then
                local position = TransformBus.Event.GetWorldTranslation(self.entityId)
                self.lastIndex = TrackMath.nearestIndex(position)
                self.lastProgress = TrackMath.progress(position, self.lastIndex)
                self.raceState = value
                self.started = true
                self.raceTime = 0.0
                self.lapTime = 0.0
                self.visitedQuarter2 = false
                self.visitedQuarter3 = false
                self.lapArmed = false
                self:SendLapChanged()
                Debug.Log(string.format("RACING start car=%.0f", self.Properties.CarIndex))
            else
                self.raceState = value
            end
        end,
    }
    table.insert(self.callbackTables, self.raceCallback)
    self.raceHandler = GameplayNotificationBus.Connect(
        self.raceCallback,
        GameplayNotificationId(self.entityId, "RaceState", "float")
    )
    self.tickHandler = TickBus.Connect(self)
end

function LapTracker:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    if self.raceHandler then
        self.raceHandler:Disconnect()
    end
    self.callbackTables = nil
end

function LapTracker:SendLapChanged()
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "LapChanged", "float"),
        self.currentLap
    )
end

function LapTracker:SendProgress(progress)
    if not self.Properties.RaceManager:IsValid() then
        return
    end
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(
            self.Properties.RaceManager,
            "Progress" .. string.format("%.0f", self.Properties.CarIndex),
            "float"
        ),
        progress
    )
end

function LapTracker:CompleteLap()
    local completedLap = self.currentLap
    local completedTime = self.lapTime
    self.completedLaps = self.completedLaps + 1
    self.bestLap = math.min(self.bestLap, completedTime)
    Debug.Log(string.format(
        "RACING lap car=%.0f lap=%d time=%.2f best=%.2f",
        self.Properties.CarIndex,
        completedLap,
        completedTime,
        self.bestLap
    ))
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "LapTime", "float"),
        completedTime
    )
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "BestLap", "float"),
        self.bestLap
    )
    if self.completedLaps >= self.Properties.Laps then
        self.finished = true
        self.raceState = 2.0
        GameplayNotificationBus.Event.OnEventBegin(
            GameplayNotificationId(self.entityId, "Finished", "float"),
            self.raceTime
        )
        Debug.Log(string.format(
            "RACING finish car=%.0f time=%.2f",
            self.Properties.CarIndex,
            self.raceTime
        ))
        return
    end
    self.currentLap = completedLap + 1
    self.lapTime = 0.0
    self.visitedQuarter2 = false
    self.visitedQuarter3 = false
    self:SendLapChanged()
end

function LapTracker:OnTick(deltaTime)
    if not self.started or self.finished or self.raceState ~= 1.0 then
        return
    end
    local dt = math.min(deltaTime, 0.1)
    self.raceTime = self.raceTime + dt
    self.lapTime = self.lapTime + dt

    local position = TransformBus.Event.GetWorldTranslation(self.entityId)
    local index = TrackMath.nearestIndex(position, self.lastIndex)
    local progress = TrackMath.progress(position, index)
    local count = #TrackData.points
    if progress >= count * 0.5 then
        self.visitedQuarter2 = true
    end
    if progress >= count * 0.75 then
        self.visitedQuarter3 = true
    end
    local wrapped = self.lastProgress > count * 0.8 and progress < count * 0.2
    if wrapped and self.visitedQuarter2 and self.visitedQuarter3 then
        if self.lapArmed then
            self:CompleteLap()
        else
            self.lapArmed = true
            self.lapTime = 0.0
            self.visitedQuarter2 = false
            self.visitedQuarter3 = false
        end
    end

    self:SendProgress(self.completedLaps * count + progress)
    self.lastIndex = index
    self.lastProgress = progress
end

return LapTracker
