-- 
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
-- 
-- SPDX-License-Identifier: Apache-2.0 OR MIT
-- 

-- ArenaShooter: score + match timer HUD (DebugDraw on-screen text; requires the
-- DebugDraw gem). Put on a single "game manager" entity. Add every entity whose
-- kills should score (targets, bots) to the Targets list.
local ScoreHud =
{
    Properties =
    {
        Targets = { default = { EntityId() }, description = "Entities whose 'Killed' events award a point" },
        MatchTime = { default = 120.0, suffix = " s" },
    },
}

function ScoreHud:OnActivate()
    self.score = 0
    self.timeLeft = self.Properties.MatchTime
    self.finished = false

    self.killHandlers = {}
    for i = 1, #self.Properties.Targets do
        local target = self.Properties.Targets[i]
        if target:IsValid() then
            local handler = {
                OnEventBegin = function(_, value) self:OnKill() end,
                OnEventUpdating = function(_, value) end,
                OnEventEnd = function(_, value) end,
            }
            table.insert(self.killHandlers,
                GameplayNotificationBus.Connect(handler, GameplayNotificationId(target, "Killed", "float")))
        end
    end
    self.tickHandler = TickBus.Connect(self)
end

function ScoreHud:OnKill()
    if not self.finished then
        self.score = self.score + 1
    end
end

function ScoreHud:OnTick(deltaTime, timePoint)
    if not self.finished then
        self.timeLeft = self.timeLeft - deltaTime
        if self.timeLeft <= 0.0 then
            self.timeLeft = 0.0
            self.finished = true
        end
    end

    local text
    if self.finished then
        text = string.format("MATCH OVER - Final score: %d", self.score)
    else
        text = string.format("Score: %d   Time: %d:%02d", self.score,
            math.floor(self.timeLeft / 60), math.floor(self.timeLeft % 60))
    end
    DebugDrawRequestBus.Broadcast.DrawTextOnScreen(text, Color(1.0, 1.0, 1.0, 1.0), 0.0)
end

function ScoreHud:OnDeactivate()
    self.tickHandler:Disconnect()
    for _, handler in ipairs(self.killHandlers) do
        handler:Disconnect()
    end
end

return ScoreHud
