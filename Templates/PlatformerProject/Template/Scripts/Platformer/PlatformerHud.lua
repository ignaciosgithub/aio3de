--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local PlatformerHud =
{
    Properties = {},
}

function PlatformerHud:OnActivate()
    self.coins = 0
    self.health = 3
    self.elapsed = 0.0
    self.goalReached = false
    self.callbackTables = {
        { OnEventBegin = function(_, value) self:OnCoinCollected(value) end },
        { OnEventBegin = function(_, value) self.health = value end },
        { OnEventBegin = function() self.goalReached = true; self:OnGoalReached() end },
        { OnEventBegin = function(_, value) self:OnStomp(value) end },
    }
    self.handlers = {
        GameplayNotificationBus.Connect(self.callbackTables[1],
            GameplayNotificationId(self.entityId, "CoinCollected", "float")),
        GameplayNotificationBus.Connect(self.callbackTables[2],
            GameplayNotificationId(self.entityId, "LifeChanged", "float")),
        GameplayNotificationBus.Connect(self.callbackTables[3],
            GameplayNotificationId(self.entityId, "GoalReached", "float")),
        GameplayNotificationBus.Connect(self.callbackTables[4],
            GameplayNotificationId(self.entityId, "Stomped", "float")),
    }
    self.tickHandler = TickBus.Connect(self)
end

function PlatformerHud:OnCoinCollected(value)
    self.coins = self.coins + value
    Debug.Log(string.format("PLATFORMER coin count=%d", self.coins))
end

function PlatformerHud:OnStomp()
    Debug.Log("PLATFORMER stomp")
end

function PlatformerHud:OnGoalReached()
    Debug.Log(string.format("PLATFORMER goal coins=%d time=%.1f", self.coins, self.elapsed))
end

function PlatformerHud:OnTick(deltaTime)
    if not self.goalReached then
        self.elapsed = self.elapsed + deltaTime
    end
    local minutes = math.floor(self.elapsed / 60.0)
    local seconds = math.floor(self.elapsed % 60.0)
    local goal = self.goalReached and "GOAL REACHED!" or "Reach the flag"
    local text = string.format(
        "Coins: %02d   Health: %d   Time: %02d:%02d\n%s",
        self.coins, self.health, minutes, seconds, goal)
    DebugDrawRequestBus.Broadcast.DrawTextOnScreen(text, Color(1.0, 1.0, 1.0, 1.0), 0.0)
end

function PlatformerHud:OnDeactivate()
    if self.tickHandler then self.tickHandler:Disconnect() end
    for _, handler in ipairs(self.handlers or {}) do
        handler:Disconnect()
    end
    self.callbackTables = nil
end

return PlatformerHud
