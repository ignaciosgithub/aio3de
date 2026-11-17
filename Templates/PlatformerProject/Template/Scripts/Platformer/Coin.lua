--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--

local CollectedCoins = require("Scripts.Platformer.CollectedCoins")

local Coin =
{
    Properties =
    {
        Radius = { default = 1.2 },
    },
}

local function coinKey(entityId)
    local position = TransformBus.Event.GetWorldTranslation(entityId)
    return string.format("%.1f,%.1f,%.1f", position.x, position.y, position.z)
end

function Coin:OnActivate()
    self.positionKey = coinKey(self.entityId)
    self.collected = CollectedCoins[self.positionKey] == true
    if self.collected then
        GameEntityContextRequestBus.Broadcast.DeactivateGameEntity(self.entityId)
        return
    end

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

function Coin:OnTick()
    if self.collected or self.playerId == nil or not self.playerId:IsValid() then
        return
    end
    local offset = TransformBus.Event.GetWorldTranslation(self.playerId)
        - TransformBus.Event.GetWorldTranslation(self.entityId)
    if offset:GetLengthSq() > self.Properties.Radius * self.Properties.Radius then
        return
    end
    self.collected = true
    CollectedCoins[self.positionKey] = true
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.playerId, "CoinCollected", "float"), 1.0)
    GameEntityContextRequestBus.Broadcast.DeactivateGameEntity(self.entityId)
end

function Coin:OnDeactivate()
    if self.tickHandler then self.tickHandler:Disconnect() end
    if self.playerBeaconHandler then self.playerBeaconHandler:Disconnect() end
    self.playerBeaconCallback = nil
end

return Coin
