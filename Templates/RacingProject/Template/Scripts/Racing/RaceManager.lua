--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local RaceManager = {
    Properties = {
        Car0 = { default = EntityId() },
        Car1 = { default = EntityId() },
        Car2 = { default = EntityId() },
        Car3 = { default = EntityId() },
        CountdownSeconds = { default = 3.0 },
    },
}

function RaceManager:OnActivate()
    self.cars = {
        self.Properties.Car0,
        self.Properties.Car1,
        self.Properties.Car2,
        self.Properties.Car3,
    }
    self.progress = { 0.0, 0.0, 0.0, 0.0 }
    self.positions = { 0, 0, 0, 0 }
    self.finished = { false, false, false, false }
    self.finishTimes = {}
    self.raceStarted = false
    self.raceElapsed = 0.0
    self.countdown = math.max(1, math.floor(self.Properties.CountdownSeconds))
    self.countdownTimer = 0.0
    self.resultsPrinted = false
    self.playerFinishDeadline = nil
    self.callbackTables = {}
    self.handlers = {}

    for index, car in ipairs(self.cars) do
        local carIndex = index
        local progressCallback = {
            OnEventBegin = function(_, value) self.progress[carIndex] = value end,
            OnEventUpdating = function(_, value) self.progress[carIndex] = value end,
        }
        local finishCallback = {
            OnEventBegin = function(_, value) self:OnCarFinished(carIndex, value) end,
        }
        table.insert(self.callbackTables, progressCallback)
        table.insert(self.callbackTables, finishCallback)
        table.insert(
            self.handlers,
            GameplayNotificationBus.Connect(
                progressCallback,
                GameplayNotificationId(self.entityId, "Progress" .. (index - 1), "float")
            )
        )
        table.insert(
            self.handlers,
            GameplayNotificationBus.Connect(
                finishCallback, GameplayNotificationId(car, "Finished", "float")
            )
        )
        self:Send(car, "RaceState", 0.0)
        self:Send(car, "Countdown", self.countdown)
    end
    Debug.Log(string.format("RACING countdown %d", self.countdown))
    self.tickHandler = TickBus.Connect(self)
end

function RaceManager:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    for _, handler in ipairs(self.handlers) do
        handler:Disconnect()
    end
    self.callbackTables = nil
end

function RaceManager:Send(car, eventName, value)
    if car ~= nil and car:IsValid() then
        GameplayNotificationBus.Event.OnEventBegin(
            GameplayNotificationId(car, eventName, "float"), value
        )
    end
end

function RaceManager:OnCarFinished(index, raceTime)
    if self.finished[index] then
        return
    end
    self.finished[index] = true
    self.finishTimes[index] = raceTime
    Debug.Log(string.format(
        "RACING manager-finish car=%d time=%.2f",
        index - 1,
        raceTime
    ))
    if index == 1 then
        self.playerFinishDeadline = self.raceElapsed + 60.0
    end
end

function RaceManager:UpdatePositions()
    local order = { 1, 2, 3, 4 }
    table.sort(order, function(first, second)
        local firstFinished = self.finished[first]
        local secondFinished = self.finished[second]
        if firstFinished ~= secondFinished then
            return firstFinished
        end
        if firstFinished then
            local firstTime = self.finishTimes[first] or math.huge
            local secondTime = self.finishTimes[second] or math.huge
            if firstTime == secondTime then
                return first < second
            end
            return firstTime < secondTime
        end
        if self.progress[first] == self.progress[second] then
            return first < second
        end
        return self.progress[first] > self.progress[second]
    end)
    for position, carIndex in ipairs(order) do
        if self.positions[carIndex] ~= position then
            self.positions[carIndex] = position
            Debug.Log(string.format(
                "RACING position car=%d position=%d progress=%.2f",
                carIndex - 1,
                position,
                self.progress[carIndex]
            ))
        end
        self:Send(self.cars[carIndex], "Position", position)
    end
end

function RaceManager:PrintResults()
    if self.resultsPrinted then
        return
    end
    self.resultsPrinted = true
    self:UpdatePositions()
    Debug.Log("RACING results")
    for position = 1, 4 do
        for carIndex, carPosition in ipairs(self.positions) do
            if carPosition == position then
                Debug.Log(string.format(
                    "RACING result position=%d car=%d time=%.2f",
                    position,
                    carIndex - 1,
                    self.finishTimes[carIndex] or self.raceElapsed
                ))
            end
        end
    end
end

function RaceManager:OnTick(deltaTime)
    self.countdownTimer = self.countdownTimer + deltaTime
    while self.countdown > 0 and self.countdownTimer >= 1.0 do
        self.countdownTimer = self.countdownTimer - 1.0
        self.countdown = self.countdown - 1
        if self.countdown > 0 then
            Debug.Log(string.format("RACING countdown %d", self.countdown))
            for _, car in ipairs(self.cars) do
                self:Send(car, "Countdown", self.countdown)
            end
        else
            self.raceStarted = true
            self.raceElapsed = 0.0
            Debug.Log("RACING go")
            for _, car in ipairs(self.cars) do
                self:Send(car, "Countdown", 0.0)
                self:Send(car, "RaceState", 1.0)
            end
        end
    end
    if not self.raceStarted then
        return
    end

    self.raceElapsed = self.raceElapsed + deltaTime
    self:UpdatePositions()
    local allFinished = true
    for _, finished in ipairs(self.finished) do
        allFinished = allFinished and finished
    end
    if allFinished
        or (self.playerFinishDeadline ~= nil and self.raceElapsed >= self.playerFinishDeadline)
    then
        self:PrintResults()
    end
end

return RaceManager
