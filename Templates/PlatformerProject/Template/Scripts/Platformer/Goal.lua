--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local Goal =
{
    Properties =
    {
        Radius = { default = 3.0 },
    },
}

function Goal:OnActivate()
    self.reached = false
    self.playerId = EntityId()
    self.playerBeaconCallback =
    {
        OnEventBegin = function(_, playerId)
            if playerId:IsValid() then
                self.playerId = playerId
            end
        end,
    }
    self.playerBeaconHandler = GameplayNotificationBus.Connect(
        self.playerBeaconCallback,
        GameplayNotificationId(EntityId(), "PlatformerPlayer", "EntityId"))
    self.tickHandler = TickBus.Connect(self)
end

function Goal:OnTick()
    if self.reached or self.playerId == nil or not self.playerId:IsValid() then
        return
    end
    local offset = TransformBus.Event.GetWorldTranslation(self.playerId)
        - TransformBus.Event.GetWorldTranslation(self.entityId)
    if offset:GetLengthSq() > self.Properties.Radius * self.Properties.Radius then
        return
    end
    self.reached = true
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.playerId, "GoalReached", "float"), 1.0)
end

function Goal:OnDeactivate()
    if self.tickHandler then self.tickHandler:Disconnect() end
    if self.playerBeaconHandler then self.playerBeaconHandler:Disconnect() end
    self.playerBeaconCallback = nil
end

return Goal
