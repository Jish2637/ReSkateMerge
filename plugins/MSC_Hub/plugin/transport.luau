-- Only this module sends packets. No mutation is retried, including after reload.
local Transport = {}
Transport.__index = Transport

function Transport.new(r)
    return setmetatable({r = r, key = "", counter = 0, revision = 0, serial = 0,
        opened = -1, lastHello = -1e9, received = -1e9, sent = -1e9,
        status = "Open the Hub while connected to an MSC server.", section = "shop"}, Transport)
end

function Transport:reset(key)
    self.key = key
    self.counter += 1
    self.nonce = tostring(math.floor(self.r.time.now())) .. "-" .. tostring(self.counter) .. "-" .. tostring(math.random(100000, 999999999))
    self.token, self.data, self.pending, self.parts, self.needsHandshake = nil, nil, nil, nil, false
    self.revision, self.serial, self.opened = 0, 0, -1
    self.received, self.lastHello = -1e9, -1e9
    self.status = "Connect to an MSC server with the Hub plugin bridge."
    if self.onReset then self.onReset() end
end

function Transport:observe()
    local key = ""
    if self.r.session.active() and self.r.session.is_dedicated() then
        local me = self.r.session.me()
        local world = self.r.session.world()
        if me then key = me.id .. ":" .. world.world end
    end
    if key ~= self.key then self:reset(key) end
    return key ~= ""
end

function Transport:hello()
    if not self:observe() then return false end
    local now = self.r.time.now()
    if now - self.lastHello < 2000 then return false end
    self.lastHello = now
    if self.opened < 0 then self.opened = now end
    return self.r.net.send("server", self.r.json.encode({v = 1, op = "hello", nonce = self.nonce}))
end

function Transport:ready()
    return self.data ~= nil and self.r.time.now() - self.received < 20000 and self.pending == nil
end

function Transport:request(body, mutation)
    if not self:observe() or not self.token or (mutation and not self:ready()) then return false end
    if self.pending then return false end
    -- The original MSC handler deduplicates this serial across both transports.
    self.serial += 1
    body.v, body.epoch, body.request = 1, self.data.epoch, self.serial
    body.slots_ui, body.section = true, self.section
    local payload = self.r.json.encode({v = 1, op = "request", nonce = self.nonce, token = self.token, body = body})
    if #payload > 4082 then self.status = "Request is too large. Shorten the fields."; return false end
    local ok = self.r.net.send("server", payload)
    if ok then
        self.sent = self.r.time.now()
        self.pending = self.serial
        self.status = "Waiting for the server..."
    else self.status = "Request could not be queued. Refresh before trying again." end
    return ok
end

function Transport:receive(from, payload)
    if not self:observe() or not from.server or type(payload) ~= "string" or #payload > 4082 then return end
    local nonce, token, revision, index, count, part = payload:match("^MSC1|([%d%-]+)|(%d+)|(%d+)|(%d+)|(%d+)|(.*)$")
    if nonce ~= self.nonce then return end
    revision, index, count = tonumber(revision), tonumber(index), tonumber(count)
    if not revision or revision <= self.revision or not count or count < 1 or count > 6 or not index or index < 1 or index > count or #part > 3000 then return end
    if self.token and token ~= self.token then return end
    local now = self.r.time.now()
    local a = self.parts
    if a and revision < a.revision then return end
    if not a or a.revision ~= revision or now - a.started > 5000 then
        a = {revision = revision, count = count, token = token, started = now, bytes = 0, chunks = {}}
        self.parts = a
    end
    if count ~= a.count or token ~= a.token then return end
    if a.chunks[index] and a.chunks[index] ~= part then self.parts = nil; return end
    if not a.chunks[index] then a.bytes += #part end
    if a.bytes > 18000 then self.parts = nil; return end
    a.chunks[index] = part
    for i = 1, count do if not a.chunks[i] then return end end
    self.parts = nil
    local ok, nextData = pcall(self.r.json.decode, table.concat(a.chunks))
    if not ok or type(nextData) ~= "table" or nextData.v ~= 1 or nextData.bridge ~= 1 or nextData.revision ~= revision
        or type(nextData.epoch) ~= "string" or nextData.player ~= self.r.session.me().id
        or type(nextData.profile) ~= "table" or type(nextData.next_request) ~= "number" then
        self.status = "Invalid MSC server response. Refresh to retry."; return
    end
    if self.data and nextData.epoch ~= self.data.epoch then self:reset(self.key); return end
    self.token, self.data, self.revision, self.received = token, nextData, revision, now
    self.needsHandshake = false
    self.serial = math.max(self.serial, nextData.next_request)
    if nextData.request == self.pending then self.pending = nil end
    if not self.pending then self.status = nextData.message or "" end
    if self.onData then self.onData(nextData) end
end

function Transport:tick()
    self:observe()
    local now = self.r.time.now()
    if self.parts and now - self.parts.started > 5000 then self.parts = nil end
    if self.pending and now - self.sent > 8000 then
        self.pending, self.received = nil, -1e9
        self.needsHandshake = true
        self.status = "No acknowledgement. Refresh and check the result before retrying."
    end
    if self.visible and now - self.visible < 1000 then
        if not self.data then
            self:hello()
            if self.opened >= 0 and now - self.opened > 5000 then
                self.status = "No MSC bridge response. Update the MSC server and use ReSkate 2.1.2 or newer with MSC Hub enabled."
            end
        elseif now - self.sent > 15000 then self:request({op = self.section == "shop" and "snapshot" or self.section}, false) end
    elseif self.wasVisible and self.token and not self.pending then
        self:request({op = "close"}, false)
        self.wasVisible = false
    end
end

return Transport
