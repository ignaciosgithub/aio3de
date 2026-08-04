-- ArenaShooter: team voice chat (requires the VoiceChat gem and, for the
-- microphone, the Microphone gem). Put on the game manager entity.
-- Hold V / gamepad d-pad up ("VoiceTalk") to talk (push-to-talk);
-- press M ("VoiceMute") to mute/unmute incoming voice.
-- Set VoiceServerAddress to the dedicated server's IP (the server hosts the
-- relay with voice_host true) and TeamChannel to the player's team id.
local VoiceChat =
{
    Properties =
    {
        VoiceServerAddress = { default = "127.0.0.1", description = "Voice relay address (usually the game server's IP)" },
        VoicePort = { default = 33452, description = "Voice relay UDP port (voice_port on the server)" },
        TeamChannel = { default = 0, description = "Team channel id; only same-channel players hear each other" },
        ConnectOnActivate = { default = true, description = "Connect to the relay when this entity activates" },
        VoiceVolume = { default = 1.0, description = "Playback volume applied independently to each remote talker" },
    },
}

function VoiceChat:OnActivate()
    self.talkHandler = InputEventNotificationBus.Connect({
        OnPressed = function(_, v) VoiceChatRequestBus.Broadcast.SetTalking(true) end,
        OnHeld = function(_, v) end,
        OnReleased = function(_, v) VoiceChatRequestBus.Broadcast.SetTalking(false) end,
    }, InputEventNotificationId("VoiceTalk"))

    self.muteHandler = InputEventNotificationBus.Connect({
        OnPressed = function(_, v)
            local muted = VoiceChatRequestBus.Broadcast.IsMuted()
            VoiceChatRequestBus.Broadcast.SetMuted(not muted)
        end,
        OnHeld = function(_, v) end,
        OnReleased = function(_, v) end,
    }, InputEventNotificationId("VoiceMute"))

    self.talkerHandler = VoiceChatNotificationBus.Connect(self)

    if self.Properties.ConnectOnActivate then
        VoiceChatRequestBus.Broadcast.SetVoiceVolume(self.Properties.VoiceVolume)
        VoiceChatRequestBus.Broadcast.ConnectVoice(
            self.Properties.VoiceServerAddress, self.Properties.VoicePort, self.Properties.TeamChannel)
    end
end

function VoiceChat:OnTalkerActive(talkerId, active)
    if active then
        Debug.Log("VoiceChat: talker " .. tostring(talkerId) .. " speaking")
    end
end

function VoiceChat:OnDeactivate()
    VoiceChatRequestBus.Broadcast.DisconnectVoice()
    self.talkHandler:Disconnect()
    self.muteHandler:Disconnect()
    self.talkerHandler:Disconnect()
end

return VoiceChat
