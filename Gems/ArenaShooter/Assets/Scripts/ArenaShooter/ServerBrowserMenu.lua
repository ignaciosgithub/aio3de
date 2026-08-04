-- ArenaShooter: in-game server browser (DebugDraw overlay; requires the
-- DebugDraw and ServerBrowser gems). Put on the game manager entity and set
-- sb_master_url (e.g. in a client autoexec cfg) to your master server.
-- F9 / gamepad back ("BrowserToggle") opens it and refreshes; up/down
-- ("MenuUp"/"MenuDown") select; Enter / gamepad A ("MenuSelect") joins;
-- left/right ("MenuLeft"/"MenuRight") refresh the list.
local ServerBrowserMenu =
{
    Properties = {},
}

local function connectPress(eventName, fn)
    return InputEventNotificationBus.Connect({
        OnPressed = function(_, v) fn(v) end,
        OnHeld = function(_, v) end,
        OnReleased = function(_, v) end,
    }, InputEventNotificationId(eventName))
end

function ServerBrowserMenu:OnActivate()
    self.open = false
    self.row = 1
    self.servers = {}
    self.pending = {}
    self.status = "press left/right to refresh"

    self.browserHandler = ServerBrowserNotificationBus.Connect(self)
    self.handlers = {
        connectPress("BrowserToggle", function()
            self.open = not self.open
            if self.open then self:Refresh() end
        end),
        connectPress("MenuUp", function() if self.open and #self.servers > 0 then self.row = ((self.row - 2) % #self.servers) + 1 end end),
        connectPress("MenuDown", function() if self.open and #self.servers > 0 then self.row = (self.row % #self.servers) + 1 end end),
        connectPress("MenuLeft", function() if self.open then self:Refresh() end end),
        connectPress("MenuRight", function() if self.open then self:Refresh() end end),
        connectPress("MenuSelect", function() self:Join() end),
    }
    self.tickHandler = TickBus.Connect(self)
end

function ServerBrowserMenu:Refresh()
    self.pending = {}
    self.status = "refreshing..."
    ServerBrowserRequestBus.Broadcast.RefreshServerList()
end

function ServerBrowserMenu:Join()
    if not self.open then return end
    local server = self.servers[self.row]
    if server then
        self.status = string.format("joining %s:%d...", server.address, server.port)
        ServerBrowserRequestBus.Broadcast.JoinServer(server.address, server.port)
    end
end

-- ServerBrowserNotificationBus
function ServerBrowserMenu:OnServerListEntry(name, address, port, map, players, maxPlayers)
    table.insert(self.pending, { name = name, address = address, port = port, map = map, players = players, maxPlayers = maxPlayers })
end

function ServerBrowserMenu:OnServerListRefreshed(count)
    self.servers = self.pending
    self.pending = {}
    self.row = 1
    self.status = string.format("%d server(s)", count)
end

function ServerBrowserMenu:OnServerListError(error)
    self.pending = {}
    self.status = "error: " .. error
end

function ServerBrowserMenu:OnTick(deltaTime, timePoint)
    if not self.open then return end

    DebugDrawRequestBus.Broadcast.DrawTextOnScreen("=== SERVERS (F9/back to close, enter/A to join) ===", Color(0.2, 1.0, 1.0, 1.0), 0.0)
    DebugDrawRequestBus.Broadcast.DrawTextOnScreen("  " .. self.status, Color(0.7, 0.7, 0.7, 1.0), 0.0)
    for i, server in ipairs(self.servers) do
        local marker = (i == self.row) and "> " or "  "
        local text = string.format("%s%s  %s:%d  %s  %d/%d",
            marker, server.name, server.address, server.port, server.map, server.players, server.maxPlayers)
        local color = (i == self.row) and Color(1.0, 1.0, 1.0, 1.0) or Color(0.7, 0.7, 0.7, 1.0)
        DebugDrawRequestBus.Broadcast.DrawTextOnScreen(text, color, 0.0)
    end
end

function ServerBrowserMenu:OnDeactivate()
    self.tickHandler:Disconnect()
    self.browserHandler:Disconnect()
    for _, handler in ipairs(self.handlers) do
        handler:Disconnect()
    end
end

return ServerBrowserMenu
