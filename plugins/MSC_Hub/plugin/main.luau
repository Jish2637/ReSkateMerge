local r = reskate
local supported = r.menu and type(r.menu.page) == "function" and r.ui and type(r.ui.dialog) == "function"
    and type(r.ui.close_dialog) == "function" and r.session and type(r.session.world) == "function"
    and type(r.session.is_dedicated) == "function" and r.net and type(r.net.send) == "function"
if not supported then
    local message = "MSC Hub needs the current ReSkate plugin API. Update official ReSkate to 2.1.2 or newer, then enable MSC Hub again."
    if r.menu and type(r.menu.page) == "function" then r.menu.page("MSC HUB", function(ui) ui:warn(message) end)
    else r.log(message) end
    return
end
local Transport = require("transport")
local forms = r.package.read_json("plugin/staff.json")
local net = Transport.new(r)
local tab, shopCategory, groupIndex, actionIndex, actionPage = 1, 1, 1, 1, 1
local leaderKind, selectedGame, selectedRecipient = 1, 1, ""
local shopItems, shopText, gameDetails = {}, {}, {}
local values, stakes, histories, reveals = {}, {}, {}, {}
local review, dialogAnswer, lastForm, preview = nil, nil, nil, nil
local groups = {}
local boards = {"weekly", "points", "time", "events"}
local symbols = {"CHERRY", "LEMON", "BELL", "SEVEN", "DIAMOND"}
local function now() return r.time.now() end
local function clip(value, count)
    local s = tostring(value or "")
    -- UI limits are in UTF-8 bytes. Never cut inside an encoded codepoint.
    local n = math.min(#s, count or 190)
    while n > 0 and n < #s and string.byte(s, n + 1) >= 128 and string.byte(s, n + 1) < 192 do n -= 1 end
    return string.sub(s, 1, n)
end
local function lines(ui, text)
    local s = tostring(text or "")
    while #s > 0 do local part = clip(s, 280); ui:label(part); s = string.sub(s, #part + 1) end
end
local function contains(list, value)
    for _, item in ipairs(list or {}) do if item == value then return true end end
    return false
end
local function send(body, mutation) return net:request(body, mutation) end
local function clearDialog()
    dialogAnswer = nil
    r.ui.close_dialog("msc-input")
end
net.onReset = function()
    review, preview, lastForm = nil, nil, nil
    values, stakes, histories, reveals = {}, {}, {}, {}
    selectedRecipient = ""
    shopText, gameDetails = {}, {}
    clearDialog()
end
net.onData = function(data)
    -- Ordinary snapshots must not wipe transfer drafts or the loaded-tab marker.
    if not data.staff_available then
        values.staff, lastForm = nil, nil
        if tab == 7 then
            tab, net.section, values.loadedTab = 1, "shop", nil
            clearDialog()
        end
    end
    for _, game in ipairs(data.games or {}) do
        if game.previous then
            local id = tostring(game.previous.id) .. ":" .. game.previous.state
            if histories[game.id] and histories[game.id] ~= id then reveals[game.id] = now() end
            histories[game.id] = id
        end
    end
end
local function prompt(title, text, initial, callback)
    if not net:ready() then return end
    local key, token = net.key, net.token
    dialogAnswer = function(value)
        if net.key == key and net.token == token and net:ready() then callback(value) end
    end
    local ok, why = r.ui.dialog("msc-input", {title = clip(title, 64), message = clip(text, 1024),
        field = {text = clip(initial or "", 256), max = 256},
        buttons = {{id = "save", label = "Use value"}, {id = "cancel", label = "Cancel"}}})
    if not ok then dialogAnswer = nil; net.status = tostring(why or "Text entry unavailable.") end
end
r.on("throwdown_ui", function(event)
    if event.action ~= "dialog" or not event.args or event.args[1] ~= "msc-input" then return end
    local callback = dialogAnswer
    clearDialog()
    if event.args[2] == "save" and callback then callback(event.args[3] or "") end
end)
local function button(ui, id, label, enabled, action)
    if enabled then if ui:button(id, clip(label, 60)) then action() end end
end
-- API 1 has no disabled-button flag. Keep primary actions visible and refuse
-- blocked clicks locally with an explanation rather than making them disappear.
local function actionButton(ui, id, label, reason, action)
    if ui:button(id, clip(label, 60), true) then
        if reason then net.status = reason else action() end
    end
    if reason then ui:note(clip(reason, 300)) end
end
local function connectionReason()
    if net.pending then return "Waiting for the server to finish the last request." end
    if not net:ready() then return "Refresh server data before sending points or placing a bet." end
    if net.data.paused then return "Server player data is paused. Transactions are unavailable." end
end
local function wholeAmount(text)
    if type(text) ~= "string" or not text:match("^%d+$") then return nil end
    local amount = tonumber(text)
    if not amount or amount <= 0 or amount > 9000000000000000 or amount ~= math.floor(amount) then return nil end
    return amount
end
local function field(ui, id, label, value, changed, secret)
    ui:info(clip(label, 60), secret and (value == "" and "Not entered" or "Entered (hidden)") or clip(value == "" and "Not entered" or value))
    button(ui, id, "Edit " .. clip(label, 54), net:ready(), function()
        prompt(label, secret and "This ReSkate dialog shows typed text. Check your surroundings before entering a password." or "Enter a single-line value.", secret and "" or value, changed)
    end)
end
local function pageButtons(ui, page, pages, action)
    ui:info("Page", tostring(page) .. " / " .. tostring(pages))
    button(ui, "previous", "Previous page", net:ready() and page > 1, function() action(page - 1) end)
    button(ui, "next", "Next page", net:ready() and page < pages, function() action(page + 1) end)
end
local function confirm(ui)
    if not review then return end
    ui:card("REVIEW ACTION")
    lines(ui, review.message)
    local captured = review
    local reason = connectionReason()
    if captured.token ~= net.token or now() - captured.created >= 30000 then reason = "This transfer review expired. Select Send points again." end
    actionButton(ui, "review-confirm", "Confirm send " .. captured.body.amount .. " MSC", reason, function()
        if captured.valid and not captured.valid() then review = nil; net.status = "State changed. Review the action again."; return end
        if send(captured.body, true) then review = nil end
    end)
    button(ui, "review-cancel", "Cancel", true, function() review = nil end)
end
local function reserve(body, message, valid)
    review = {body = body, message = message, token = net.token, created = now(), valid = valid}
end

local function shop(ui, data)
    local categoryNames = {"Colours", "Shimmers", "Tags & Messages", "Bundles"}
    local chosenCategory = ui:dropdown("shop-category", "Category", categoryNames, shopCategory)
    if chosenCategory ~= shopCategory then shopCategory = chosenCategory; clearDialog() end
    local products, names = {}, {}
    for _, product in ipairs(data.products or {}) do
        local category = product.id:match("^colour:") and 1 or product.id:match("^shimmer:") and 2
            or (product.id == "tag" or product.id == "joinmsg") and 3 or 4
        if category == shopCategory then
            table.insert(products, product)
            table.insert(names, clip(product.label .. (product.equipped and " (equipped)" or product.owned and " (owned)" or ""), 60))
        end
    end
    if #products == 0 then ui:note("No items in this category are available from this server.")
    else
        local selected = 1
        for i, product in ipairs(products) do if product.id == shopItems[shopCategory] then selected = i end end
        selected = ui:dropdown("shop-item", "Item", names, selected)
        local product = products[selected]
        if shopItems[shopCategory] ~= product.id then clearDialog() end
        shopItems[shopCategory] = product.id
        local isText = product.id == "tag" or product.id == "joinmsg"
        local kind = (product.id:match("^colour:") or product.id:match("^shimmer:")) and "colour" or product.id
        ui:card(clip(product.label, 60))
        local status = product.equipped and "Equipped" or product.owned and "Owned" or "Not owned"
        ui:info("Price / ownership", tostring(product.price) .. " MSC / " .. status)
        if product.id:match("^colour:") and not product.owned then ui:note("Price applies per colour change unless you own the bundle.") end
        if isText then
            shopText[product.id] = shopText[product.id] or ""
            field(ui, "shop-text", product.label, shopText[product.id], function(value)
                shopText[product.id] = value
                preview = {text = value, label = product.label}
            end)
            preview = {text = shopText[product.id], label = product.label}
        else preview = {colour = product.colour, text = product.value, label = product.label} end
        local reason = connectionReason()
        if not product.enabled then reason = product.reason ~= "" and product.reason or "This item is unavailable under the current server settings." end
        if product.equipped then reason = "This item is already equipped." end
        if isText and shopText[product.id] == "" then reason = "Enter text above to preview or purchase this item." end
        local label = product.equipped and "Equipped" or product.owned and (isText and "Apply text" or "Equip") or "Buy for " .. product.price .. " MSC"
        actionButton(ui, "shop-apply", label, reason, function()
            send({op = "item", kind = kind, value = isText and shopText[product.id] or product.value or ""}, true)
        end)
    end
    if data.quote then
        local q = data.quote
        ui:card("CONFIRM PURCHASE", tostring(q.price) .. " MSC")
        lines(ui, q.kind .. ": " .. q.value)
        local seconds = math.max(0, q.seconds - (now() - net.received) / 1000)
        ui:info("Quote expires", tostring(math.ceil(seconds)) .. " seconds")
        local reason = connectionReason() or (seconds <= 0 and "This quote expired. Request a fresh purchase quote." or nil)
        actionButton(ui, "purchase-confirm", "Confirm purchase", reason, function() send({op = "confirm", token = q.token}, true) end)
    end
end

local function collection(ui, data)
    ui:card("OWNED TAGS")
    for i, tag in ipairs(data.tags or {}) do
        lines(ui, tag.text .. (tag.equipped and " (equipped)" or ""))
        button(ui, "tag-preview-" .. i, "Preview tag", true, function() preview = {label = "Custom tag", text = tag.text} end)
        button(ui, "tag-" .. i, "Equip tag", net:ready() and tag.enabled and not tag.equipped, function() send({op = "item", kind = "tag", value = tag.text}, true) end)
    end
    pageButtons(ui, data.collection_page, data.collection_pages, function(page) send({op = "collection", page = page}, false) end)
    button(ui, "tag-off", "Hide equipped tag", net:ready() and data.can_tag, function() send({op = "item", kind = "tag", value = "off"}, true) end)
    button(ui, "colour-default", "Default colour", net:ready() and data.shop, function() send({op = "item", kind = "colour", value = "default"}, true) end)
    ui:card("OWNED SHIMMERS")
    for i, product in ipairs(data.products or {}) do
        if product.id:match("^shimmer:") and product.owned then
            lines(ui, product.label .. (product.equipped and " (equipped)" or ""))
            button(ui, "shimmer-" .. i, "Equip " .. product.label, net:ready() and product.enabled and not product.equipped,
                function() send({op = "item", kind = "colour", value = product.value}, true) end)
        end
    end
    ui:card("JOIN MESSAGE")
    lines(ui, data.profile.joinmsg)
    button(ui, "join-toggle", data.profile.joinmsg_on and "Disable join message" or "Enable join message", net:ready() and data.can_joinmsg and data.profile.joinmsg_owned,
        function() send({op = "joinmsg-toggle", enabled = not data.profile.joinmsg_on}, true) end)
    ui:note("Owned standard colours can be equipped from Shop.")
end

local function profile(ui, data)
    local p = data.profile
    ui:card("MY MSC", clip(p.name, 120))
    for _, row in ipairs({{"Wallet", p.balance}, {"Lifetime points", p.lifetime}, {"Weekly points", p.weekly},
        {"Active seconds", p.active_seconds}, {"Daily seconds", p.daily_seconds}, {"Daily target seconds", p.daily_target},
        {"Daily reward", p.daily_done and "Claimed" or "In progress"}, {"Streak", p.streak}, {"Qualifying visits", p.visits},
        {"VIP", p.vip}, {"Seconds to VIP", p.vip_seconds}, {"Tag", p.tag}, {"Colour", p.colour}}) do ui:info(row[1], clip(row[2])) end
    for i, name in ipairs({"S.K.A.T.E.", "Spot Battle", "Jam"}) do ui:info(name .. " events", tostring((p.events or {})[i] or 0)) end
    if p.daily_target > 0 then ui:bar("Daily reward", p.daily_seconds, 0, p.daily_target) end
    ui:note("Daily rewards are automatic. The daily window resets at 00:00 UTC.")
    lines(ui, p.joinmsg)
end

local function leaderboard(ui, data)
    local chosen = ui:dropdown("ranking", "Ranking", {"Weekly points", "Lifetime points", "Active time", "Events"}, leaderKind)
    if chosen ~= leaderKind then leaderKind = chosen; values.loadedTab = nil end
    for _, row in ipairs(data.leaders or {}) do ui:info("#" .. row.rank .. " " .. clip(row.name, 45), data.leader_kind == "time" and tostring(math.floor(row.value / 3600000000)) .. " h " .. tostring(math.floor(row.value / 60000000) % 60) .. " m" or tostring(row.value)) end
    pageButtons(ui, data.leader_page, data.leader_pages, function(page) send({op = "leaderboard", kind = boards[leaderKind], page = page}, false) end)
end

local function transfer(ui, data)
    local players, names = {{id = "", name = "Select a recipient"}}, {"Select a recipient"}
    for _, player in ipairs(r.session.players()) do
        if player.id ~= data.player and not player.host then
            table.insert(players, player)
            table.insert(names, clip(player.name, 60))
        end
    end
    local selected = 1
    for i, player in ipairs(players) do if player.id == selectedRecipient then selected = i end end
    local chosen = ui:dropdown("recipient", "Recipient", names, selected)
    local target = players[chosen]
    if target.id ~= selectedRecipient then review = nil end
    selectedRecipient = target.id
    values.amount = values.amount or ""
    field(ui, "amount", "Amount (MSC)", values.amount, function(value) values.amount = value; review = nil end)
    local amount = wholeAmount(values.amount)
    local reason = connectionReason()
    if not data.can_pay then reason = "Transfers are unavailable under the current server settings or permissions."
    elseif #players == 1 then reason = "No other players are connected."
    elseif target.id == "" then reason = "Choose a recipient before sending points."
    elseif not amount then reason = "Enter a positive whole amount of MSC points."
    elseif amount > data.profile.balance then reason = "Insufficient MSC points for this transfer." end
    actionButton(ui, "transfer-send", "Send points", reason, function()
        local id, key = target.id, net.key
        reserve({op = "pay", recipient = id, amount = amount}, "Send " .. amount .. " MSC to " .. target.name .. " (" .. id .. ")?", function()
            if net.key ~= key or not net.data.can_pay or net.data.profile.balance < amount then return false end
            for _, player in ipairs(r.session.players()) do if player.id == id and not player.host then return true end end
            return false
        end)
    end)
end

local function gameBody(game, action, stake)
    return {op = "game", game = game.id, action = action, round = game.round and game.round.id or 0, stake = stake or 0}
end
local function gameReason(game, action, stake)
    local reason = connectionReason()
    if reason then return reason end
    if not game.enabled then return game.reason ~= "" and game.reason or "This activity is disabled on the server." end
    if game.id == "slots" and (game.cooldown or 0) > 0 then return "Next spin available in " .. game.cooldown .. " seconds." end
    if not contains(game.actions, action) then return "This action is not currently available. Wait for fresh server state." end
    if stake then
        if type(stake) ~= "number" or stake ~= stake or stake <= 0 or stake > 9000000000000000 or stake ~= math.floor(stake) then return "Enter a positive whole bet amount." end
        if stake < game.minimum or stake > game.maximum then return "Bet must be between " .. game.minimum .. " and " .. game.maximum .. " MSC." end
        if stake > net.data.profile.balance then return "Insufficient MSC points for this bet." end
    end
end
local function activities(ui, data)
    local games, names = data.games or {}, {}
    for _, game in ipairs(games) do table.insert(names, clip(game.label, 60)) end
    if #games == 0 then ui:note("Loading activities..."); return end
    selectedGame = ui:dropdown("game", "Activity", names, math.min(selectedGame, #games))
    local game = games[selectedGame]
    local details = gameDetails[game.id] or {}; gameDetails[game.id] = details
    ui:card(clip(game.label, 60), "Bet limits " .. game.minimum .. " - " .. game.maximum .. " MSC")
    if not game.round or game.id == "slots" then
        stakes[game.id] = stakes[game.id] or tostring(game.minimum)
        field(ui, "stake", "Bet amount (MSC)", stakes[game.id], function(value) stakes[game.id] = value end)
        local amount = wholeAmount(stakes[game.id])
        local action = game.id == "slots" and "spin" or "create"
        local label = game.id == "slots" and "Spin" or game.id == "highlow" and "Draw first card" or "Create lobby"
        local reason = gameReason(game, action, amount or 0)
        if not amount and not connectionReason() then reason = "Enter a positive whole bet amount." end
        actionButton(ui, "game-create", label .. " - " .. (amount or "set bet") .. " MSC", reason, function()
            send(gameBody(game, action, amount), true)
        end)
    else
        local round = game.round
        ui:info("Round / bet / pot", "#" .. round.id .. " / " .. round.stake .. " / " .. round.pot .. " MSC")
        local seconds = math.max(0, math.ceil(round.seconds - (now() - net.received) / 1000))
        if game.id == "highlow" then ui:info("First card", tostring(round.initial))
        else ui:info("Players", tostring(round.stake > 0 and round.pot / round.stake or #round.members) .. " / " .. round.capacity) end
        local labels = {join = "Join - " .. round.stake .. " MSC", start = "Start game", leave = "Leave and refund", cancel = "Cancel and refund", higher = "Higher", lower = "Lower"}
        for _, action in ipairs(game.actions or {}) do
            local stake = action == "join" and round.stake or nil
            local reason = gameReason(game, action, stake)
            if seconds <= 0 then reason = "This round has expired. Wait for the server to refresh." end
            actionButton(ui, "game-" .. action, labels[action] or action, reason, function()
                send(gameBody(game, action, stake), true)
            end)
        end
        details.round = ui:toggle("round-details", "Round details", details.round or false)
        if details.round then
            ui:info("Refund timeout", tostring(seconds) .. " seconds")
            for _, member in ipairs(round.members or {}) do lines(ui, member.name .. (member.id == round.creator and " (creator)" or "")) end
        end
    end
    local previous = game.previous
    if previous then
        local elapsed = now() - (reveals[game.id] or -1e9)
        local participated = false
        for _, member in ipairs(previous.members or {}) do if member.id == data.player then participated = true end end
        local returned = previous.state == "refunded" or previous.my_payout == previous.stake
        local title = not participated and "ROUND COMPLETE" or returned and "STAKE RETURNED" or previous.my_payout > previous.stake and "YOU WON" or "YOU LOST"
        ui:card(elapsed < 2500 and "RESULT" or title, "Round #" .. previous.id)
        if elapsed < 2500 then ui:note("Revealing the recorded result...")
        else
            if participated then ui:info("Net change", (previous.my_payout > previous.stake and "+" or "") .. tostring(previous.my_payout - previous.stake) .. " MSC") end
            if game.id == "highlow" then ui:info("Cards", previous.initial .. " -> " .. previous.second)
            elseif game.id == "slots" then
                local labels = {}; for _, reel in ipairs(previous.reels or {}) do table.insert(labels, symbols[reel + 1] or "?") end
                ui:info("Reels", table.concat(labels, " | "))
            end
        end
        details.result = ui:toggle("result-details", "Result details", details.result or false)
        if details.result and elapsed >= 2500 then
            lines(ui, previous.result)
            if participated then ui:info("Payout / bet", previous.my_payout .. " / " .. previous.stake .. " MSC") end
            for _, member in ipairs(previous.members or {}) do if member.id == previous.winner then ui:info("Winner", clip(member.name)) end end
            ui:info("Pot", tostring(previous.pot))
            if game.id == "highlow" then ui:info("Choice", previous.direction) end
            if game.id == "slots" then ui:info("Multiplier", tostring(previous.multiplier) .. "x") end
            for _, roll in ipairs(previous.rolls or {}) do ui:info("Dice round " .. roll.round, roll.player .. ": " .. roll.value) end
        end
    end
    details.help = ui:toggle("game-help", "Rules & payouts", details.help or false)
    if details.help then
        if game.id == "slots" then ui:note("Any pair returns the bet. Triples: Cherry 3x, Lemon 5x, Bell 8x, Seven 15x, Diamond 25x.")
        elseif game.id == "highlow" then ui:note("Numbers are 1-13. Predict higher or lower; ties refund. A winning payout is 12 x bet / winning numbers, with fair rounding.")
        else ui:note("Create or join an equal-bet lobby. Its creator starts the game. The winner receives the pot; leaving or cancelling follows the server refund rules.") end
        if data.profile.unlimited then ui:note("Games use your real wallet balance, including for administrators.") end
        if game.id == "slots" then for _, win in ipairs(game.wins or {}) do lines(ui, win.name .. " won " .. win.payout .. " MSC (bet " .. win.stake .. ")") end end
    end
end

local function staff(ui, data)
    local s = data.staff
    if not s then ui:note("Loading authorized staff tools..."); return end
    ui:info("Your role", s.role)
    for _, player in ipairs(s.players or {}) do
        lines(ui, player.name .. " | " .. player.role .. " / " .. player.rank .. " | voice: " .. (player.voice_muted and "muted" or "on") .. " | chat: " .. (player.text_muted and "timeout" or "on") .. " | locks: " .. player.locks)
    end
    pageButtons(ui, s.page, s.pages, function(page) send({op = "staff", page = page}, false) end)
    groups = {}
    for _, form in ipairs(forms) do if contains(s.actions, form.id) and not contains(groups, form.group) then table.insert(groups, form.group) end end
    if #groups == 0 then ui:note("No forms are available for your role."); return end
    local newGroup = ui:dropdown("staff-group", "Category", groups, math.min(groupIndex, #groups))
    if newGroup ~= groupIndex then groupIndex, actionIndex, actionPage = newGroup, 1, 1; clearDialog() end
    local actions, names = {}, {}
    for _, form in ipairs(forms) do if form.group == groups[groupIndex] and contains(s.actions, form.id) then table.insert(actions, form) end end
    local pages = math.max(1, math.ceil(#actions / 24))
    actionPage = math.clamp(actionPage, 1, pages)
    if pages > 1 then
        local choices = {}; for i = 1, pages do table.insert(choices, "Actions " .. i) end
        local page = ui:dropdown("action-page", "Action page", choices, actionPage)
        if page ~= actionPage then actionPage, actionIndex = page, 1 end
    end
    local shown = {}
    for i = (actionPage - 1) * 24 + 1, math.min(#actions, actionPage * 24) do table.insert(shown, actions[i]); table.insert(names, clip(actions[i].label, 60)) end
    actionIndex = ui:dropdown("staff-action", "Action", names, math.min(actionIndex, #shown))
    local form = shown[actionIndex]
    if form.id ~= lastForm then lastForm = form.id; values.staff = {}; clearDialog() end
    local fields = values.staff or {}; values.staff = fields
    ui:card(clip(form.label, 60))
    if s.settings and s.settings[form.id] then lines(ui, "Current: " .. s.settings[form.id]) end
    local complete = true
    for i, spec in ipairs(form.fields) do
        local label = spec.label .. (spec.optional and " (optional)" or "")
        fields[i] = fields[i] or ""
        local choices = spec.type == "role" and s.roles or spec.choices
        if spec.type == "player" then
            choices = {}; local ids = {}
            if spec.optional then table.insert(choices, "Default"); table.insert(ids, "") end
            for _, player in ipairs(s.players) do table.insert(choices, clip(player.name, 60)); table.insert(ids, player.id) end
            if #choices > 0 then
                local selected = 1; for j, id in ipairs(ids) do if id == fields[i] then selected = j end end
                fields[i] = ids[ui:dropdown("staff-field-" .. i, clip(label, 60), choices, selected)]
            end
        elseif spec.type == "role" or spec.type == "choice" then
            local labels = {}; if spec.optional then table.insert(labels, "Default") end
            for _, choice in ipairs(choices or {}) do table.insert(labels, choice) end
            if #labels > 0 and #labels <= 32 then
                local selected = 1; for j, choice in ipairs(labels) do if choice == fields[i] then selected = j end end
                local selectedValue = labels[ui:dropdown("staff-field-" .. i, clip(label, 60), labels, selected)]
                fields[i] = selectedValue == "Default" and "" or selectedValue
            else field(ui, "staff-field-" .. i, label, fields[i], function(value) fields[i] = value end) end
        else field(ui, "staff-field-" .. i, label, fields[i], function(value) fields[i] = value end, spec.type == "secret") end
        if not spec.optional and fields[i] == "" then complete = false end
    end
    button(ui, "staff-submit", form.read_only and "Run query" or "Review action", net:ready() and complete, function()
        local args = {}; for i = 1, #form.fields do args[i] = fields[i] end
        -- Empty tables encode as objects in this API; the bridge normalizes only empty args.
        if send({op = form.read_only and "staff-read" or "staff-preview", action = form.id, args = args}, not form.read_only) then
            for i, spec in ipairs(form.fields) do if spec.type == "secret" then fields[i] = "" end end
        end
    end)
    if s.quote then
        ui:card("CONFIRM STAFF ACTION")
        lines(ui, s.quote.description)
        local seconds = math.max(0, s.quote.seconds - (now() - net.received) / 1000)
        ui:info("Review expires", tostring(math.ceil(seconds)) .. " seconds")
        button(ui, "staff-confirm", "Confirm reviewed action", net:ready() and seconds > 0, function() send({op = "staff-confirm", token = s.quote.token}, true) end)
        button(ui, "staff-cancel", "Discard review", net:ready(), function() send({op = "staff-cancel"}, true) end)
    end
    ui:card("SERVER OUTPUT")
    lines(ui, s.output)
end

r.menu.page("MSC HUB", function(ui)
    net.visible, net.wasVisible = now(), true
    net:observe()
    ui:heading("MSC HUB")
    if not net.data then ui:note(net.status); button(ui, "connect", "Connect / retry", true, function() net:hello() end); return end
    local data = net.data
    ui:info("Wallet", tostring(data.profile.balance) .. " MSC" .. (data.profile.unlimited and " (unlimited shop credit)" or ""))
    if net.status ~= "" then lines(ui, net.status) end
    if data.paused then ui:warn("Server player data is paused. Transactions may be unavailable.") end
    button(ui, "refresh", "Refresh from server", not net.pending, function()
        if net.needsHandshake then net:reset(net.key); net:hello(); return end
        send({op = net.section == "shop" and (tab == 4 and "leaderboard" or tab == 2 and "collection" or "snapshot") or net.section,
            kind = boards[leaderKind], page = tab == 2 and data.collection_page or tab == 4 and data.leader_page or 1}, false)
    end)
    local names = {"Shop", "Collection", "My MSC", "Leaderboard", "Transfer", "Activities"}
    if data.staff_available then table.insert(names, "Staff") end
    local chosen = ui:tabs("hub-tabs", names, tab)
    if chosen ~= tab then
        tab, review, preview = chosen, nil, nil; clearDialog()
        net.section = tab == 6 and "games" or tab == 7 and "staff" or "shop"
        -- A tab change during an in-flight request is reconciled on the next build.
        values.loadedTab = nil
    end
    if values.loadedTab ~= tab and not net.pending then
        if send({op = tab == 6 and "games" or tab == 7 and "staff" or tab == 4 and "leaderboard" or tab == 2 and "collection" or "snapshot", kind = boards[leaderKind], page = 1}, false) then values.loadedTab = tab end
    end
    if tab == 1 then shop(ui, data)
    elseif tab == 2 then collection(ui, data)
    elseif tab == 3 then profile(ui, data)
    elseif tab == 4 then leaderboard(ui, data)
    elseif tab == 5 then transfer(ui, data)
    elseif tab == 6 then activities(ui, data)
    elseif tab == 7 then staff(ui, data) end
    confirm(ui)
end)

r.on("message", function(from, payload) net:receive(from, payload) end)
r.on("tick", function() net:tick() end)
r.on("level_unloading", function() net:reset("") end)
r.on("unload", function() clearDialog() end)

-- Procedural activity art uses only server-issued results. No local RNG determines a result.
r.on("draw", function(d)
    if not net.data or not net.visible or now() - net.visible > 500 then return end
    if (tab == 1 or tab == 2) and preview then
        local size = d:size()
        local x, y = math.max(20, size.x - 380), size.y - 120
        local function point(a, b) return vector.create(a, b, 0) end
        d:rect(point(x - 12, y - 10), point(x + 350, y + 75), {fill = true, colour = "#10242CEE", rounding = 10})
        d:text(point(x, y), "MSC NAME PREVIEW", {colour = "#65DFEF", size = 16})
        local p = net.data.profile
        local colour = preview.colour or 0
        -- Server catalogue colours use ImGui's ABGR representation.
        local rgba = bit32.band(colour, 255) * 16777216 + bit32.band(bit32.rshift(colour, 8), 255) * 65536 + bit32.band(bit32.rshift(colour, 16), 255) * 256 + 255
        local name = (p.tag ~= "" and "[" .. p.tag .. "] " or "") .. p.name
        if preview.label == "Custom tag" then name = "[" .. (preview.text or "TAG") .. "] " .. p.name end
        local shimmer = (preview.text or ""):find("shimmer", 1, true)
        if shimmer then
            local amount = (math.sin(now() / 350) + 1) / 2
            local red, green, blue = bit32.band(colour, 255), bit32.band(bit32.rshift(colour, 8), 255), bit32.band(bit32.rshift(colour, 16), 255)
            rgba = math.floor(red + (255 - red) * amount) * 16777216 + math.floor(green + (255 - green) * amount) * 65536 + math.floor(blue + (255 - blue) * amount) * 256 + 255
        end
        d:text(point(x, y + 30), clip(name, 120), {size = 24, colour = colour == 0 and "#FFFFFF" or rgba})
        return
    end
    if tab ~= 6 then return end
    local game = (net.data.games or {})[selectedGame]
    if not game or not game.previous then return end
    local round = game.previous
    local elapsed = now() - (reveals[game.id] or -1e9)
    local animating = elapsed < 2500
    local size = d:size()
    local x, y = math.max(30, size.x - 360), size.y - 200
    local function point(a, b) return vector.create(a, b, 0) end
    d:rect(point(x - 16, y - 16), point(x + 310, y + 125), {fill = true, colour = "#10242CEE", rounding = 12})
    d:text(point(x, y), clip(game.label, 30), {colour = "#65DFEF", size = 20})
    if game.id == "coinflip" then
        local radius = animating and (12 + 24 * math.abs(math.cos(elapsed / 90))) or 36
        d:circle(point(x + 55, y + 65), radius, {fill = true, colour = "#F6C340"})
        d:text(point(x + 105, y + 55), animating and "FLIPPING" or "RESULT RECORDED", {size = 16})
    elseif game.id == "slots" then
        for i = 1, 3 do
            local reel = animating and math.floor(elapsed / 80 + i) % 5 or (round.reels or {})[i]
            d:rect(point(x + (i - 1) * 100, y + 35), point(x + (i - 1) * 100 + 94, y + 95), {fill = true, colour = "#176071", rounding = 8})
            d:text(point(x + (i - 1) * 100 + 5, y + 55), symbols[(reel or 0) + 1], {size = 14, colour = "#FFFFFF"})
        end
    elseif game.id == "highlow" then
        d:text(point(x + 20, y + 50), tostring(round.initial) .. "  ->  " .. (animating and "?" or tostring(round.second)), {size = 40, colour = "#FFFFFF"})
    elseif game.id == "dice" then
        for i, roll in ipairs(round.rolls or {}) do
            if i > 3 then break end
            local face = animating and (math.floor(elapsed / 90 + i) % 6 + 1) or roll.value
            local dx = x + (i - 1) * 95
            d:rect(point(dx, y + 35), point(dx + 70, y + 105), {fill = true, colour = "#E3F6F9", rounding = 8})
            local pips = {{4}, {1, 7}, {1, 4, 7}, {1, 2, 6, 7}, {1, 2, 4, 6, 7}, {1, 2, 3, 5, 6, 7}}
            local places = {{18, 18}, {52, 18}, {18, 35}, {35, 35}, {52, 35}, {18, 52}, {52, 52}}
            for _, pip in ipairs(pips[face]) do d:circle(point(dx + places[pip][1], y + 35 + places[pip][2]), 5, {fill = true, colour = "#10242C"}) end
        end
    end
end)
