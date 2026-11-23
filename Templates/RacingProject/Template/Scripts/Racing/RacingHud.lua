--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local RacingHud = {}

local function formatTime(value)
    return string.format("%02d:%05.2f", math.floor(value / 60.0), value % 60.0)
end

function RacingHud:OnActivate()
    self.countdown = 3.0
    self.lap = 1
    self.position = 4
    self.bestLap = math.huge
    self.lastLap = 0.0
    self.elapsed = 0.0
    self.lapElapsed = 0.0
    self.racing = false
    self.finished = false
    self.callbackTables = {}
    self.handlers = {}

    local function listen(eventName, callback)
        local handler = {
            OnEventBegin = function(_, value) callback(value) end,
            OnEventUpdating = function(_, value) callback(value) end,
        }
        table.insert(self.callbackTables, handler)
        table.insert(
            self.handlers,
            GameplayNotificationBus.Connect(
                handler, GameplayNotificationId(self.entityId, eventName, "float")
            )
        )
    end

    listen("Countdown", function(value)
        self.countdown = value
    end)
    listen("RaceState", function(value)
        self.racing = value == 1.0
        if self.racing then
            self.elapsed = 0.0
            self.lapElapsed = 0.0
        end
    end)
    listen("LapChanged", function(value)
        self.lap = value
        self.lapElapsed = 0.0
    end)
    listen("LapTime", function(value) self.lastLap = value end)
    listen("BestLap", function(value) self.bestLap = value end)
    listen("Position", function(value) self.position = value end)
    listen("Finished", function() self.finished = true end)
    self.tickHandler = TickBus.Connect(self)
end

function RacingHud:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    for _, handler in ipairs(self.handlers) do
        handler:Disconnect()
    end
    self.callbackTables = nil
end

function RacingHud:OnTick(deltaTime)
    if self.racing and not self.finished then
        self.elapsed = self.elapsed + deltaTime
        self.lapElapsed = self.lapElapsed + deltaTime
    end
    local velocity = RigidBodyRequestBus.Event.GetLinearVelocity(self.entityId)
    local speed = velocity:GetLength() * 3.6
    local best = self.bestLap == math.huge and "--:--.--" or formatTime(self.bestLap)
    local state
    if self.finished then
        state = string.format("FINISHED - P%d  %s", self.position, formatTime(self.elapsed))
    elseif self.racing then
        state = "GO!"
    else
        state = string.format("%d", self.countdown)
    end
    DebugDrawRequestBus.Broadcast.DrawTextOnScreen(
        string.format(
            "%s\nLAP %d / 3   POS %d / 4\nTIME %s   BEST %s\nSPEED %.0f km/h   LAST LAP %s",
            state,
            self.lap,
            self.position,
            formatTime(self.lapElapsed),
            best,
            speed,
            self.lastLap > 0.0 and formatTime(self.lastLap) or "--:--.--"
        ),
        Color(1.0, 1.0, 1.0, 1.0),
        0.0
    )
end

return RacingHud
