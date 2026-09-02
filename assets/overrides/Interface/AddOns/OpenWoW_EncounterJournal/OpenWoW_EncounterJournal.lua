-- The navigation and information hierarchy follow Blizzard_EncounterJournal
-- 4.3.4. The implementation is rewritten for the build 12340 widget/API
-- surface because the Cataclysm EncounterJournal artwork and shared FrameXML
-- dependencies are not part of the supplied UI source.

local API = C_OpenWoWJournal
local REQUIRED_SCHEMA = 3
local UNKNOWN_ICON = "Interface\\Icons\\INV_Misc_QuestionMark"
local BACKDROP = {
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true,
    tileSize = 16,
    edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 },
}

OpenWoWEncounterJournalDB = OpenWoWEncounterJournalDB or {}
local state = {
    isRaid = OpenWoWEncounterJournalDB.isRaid and true or false,
    tier = OpenWoWEncounterJournalDB.tier,
    search = "",
    instanceOffset = 0,
    bossOffset = 0,
    abilityOffset = 0,
    instances = {},
    bosses = {},
    abilities = {},
    loot = {},
    detailMode = "abilities",
    difficulties = {},
    instance = nil,
    boss = nil,
    difficulty = nil,
}

local function SetPanelBackdrop(panel, red, green, blue, alpha)
    panel:SetBackdrop(BACKDROP)
    panel:SetBackdropColor(red, green, blue, alpha)
    panel:SetBackdropBorderColor(0.48, 0.40, 0.25, 0.95)
end

local function CreateLabel(parent, fontObject, text)
    local label = parent:CreateFontString(nil, "ARTWORK", fontObject)
    label:SetText(text or "")
    return label
end

local function CreateActionButton(parent, text, width, height)
    local button = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    button:SetSize(width, height)
    button:SetText(text)
    return button
end

local function CreateListRow(parent, width, height)
    local row = CreateFrame("Button", nil, parent)
    row:SetSize(width, height)
    row.background = row:CreateTexture(nil, "BACKGROUND")
    row.background:SetAllPoints(row)
    row.background:SetTexture(0.10, 0.085, 0.06, 0.78)
    row.highlight = row:CreateTexture(nil, "HIGHLIGHT")
    row.highlight:SetAllPoints(row)
    row.highlight:SetTexture(0.35, 0.28, 0.12, 0.45)
    row.text = CreateLabel(row, "GameFontHighlightSmall")
    row.text:SetPoint("LEFT", row, "LEFT", 10, 0)
    row.text:SetPoint("RIGHT", row, "RIGHT", -8, 0)
    row.text:SetJustifyH("LEFT")
    return row
end

local function ContainsText(value, query)
    if query == "" then
        return true
    end
    value = string.lower(tostring(value or ""))
    return string.find(value, query, 1, true) ~= nil
end

local function ExpansionName(tier)
    if tier == 0 then
        return "经典旧世"
    elseif tier == 1 then
        return "燃烧的远征"
    elseif tier == 2 then
        return "巫妖王之怒"
    end
    return "未知资料片"
end

local function DifficultyName(rawDifficulty, maxPlayers, isRaid)
    if isRaid then
        if rawDifficulty == 0 then
            return maxPlayers and maxPlayers > 0 and (maxPlayers .. "人") or "普通"
        elseif rawDifficulty == 1 then
            return maxPlayers and maxPlayers > 0 and (maxPlayers .. "人") or "大型团队"
        elseif rawDifficulty == 2 then
            return maxPlayers and maxPlayers > 0 and (maxPlayers .. "人英雄") or "英雄"
        elseif rawDifficulty == 3 then
            return maxPlayers and maxPlayers > 0 and (maxPlayers .. "人英雄") or "大型团队英雄"
        end
    else
        if rawDifficulty == 0 then
            return "普通"
        elseif rawDifficulty == 1 then
            return "英雄"
        end
    end
    if maxPlayers and maxPlayers > 0 then
        return maxPlayers .. "人 · 难度 " .. rawDifficulty
    end
    return "难度 " .. rawDifficulty
end

local QUALITY_COLORS = {
    [0] = { 0.62, 0.62, 0.62 },
    [1] = { 1.00, 1.00, 1.00 },
    [2] = { 0.12, 1.00, 0.00 },
    [3] = { 0.00, 0.44, 0.87 },
    [4] = { 0.64, 0.21, 0.93 },
    [5] = { 1.00, 0.50, 0.00 },
    [6] = { 0.90, 0.80, 0.50 },
    [7] = { 0.00, 0.80, 1.00 },
}

local frame = CreateFrame("Frame", "OpenWoWEncounterJournalFrame", UIParent)
frame:SetSize(920, 640)
frame:SetPoint("CENTER", UIParent, "CENTER", 0, 15)
frame:SetFrameStrata("HIGH")
frame:SetClampedToScreen(true)
frame:SetMovable(true)
frame:EnableMouse(true)
SetPanelBackdrop(frame, 0.035, 0.028, 0.018, 0.98)
frame:Hide()

local titleBar = CreateFrame("Frame", nil, frame)
titleBar:SetPoint("TOPLEFT", frame, "TOPLEFT", 8, -8)
titleBar:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -8, -8)
titleBar:SetHeight(38)
titleBar:EnableMouse(true)
titleBar:SetScript("OnMouseDown", function()
    frame:StartMoving()
end)
titleBar:SetScript("OnMouseUp", function()
    frame:StopMovingOrSizing()
end)

local title = CreateLabel(titleBar, "GameFontNormalLarge", "地下城手册")
title:SetPoint("CENTER", titleBar, "CENTER", 0, 2)

local subtitle = CreateLabel(titleBar, "GameFontHighlightSmall", "build 12340 · 现代浏览体验")
subtitle:SetPoint("TOP", title, "BOTTOM", 0, -2)
subtitle:SetTextColor(0.62, 0.62, 0.62)

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -3, -3)
close:SetScript("OnClick", function()
    frame:Hide()
end)

local leftPanel = CreateFrame("Frame", nil, frame)
leftPanel:SetPoint("TOPLEFT", frame, "TOPLEFT", 12, -52)
leftPanel:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 12, 12)
leftPanel:SetWidth(270)
SetPanelBackdrop(leftPanel, 0.055, 0.045, 0.028, 0.96)

local dungeonTab = CreateActionButton(leftPanel, "地下城", 114, 24)
dungeonTab:SetPoint("TOPLEFT", leftPanel, "TOPLEFT", 12, -12)
local raidTab = CreateActionButton(leftPanel, "团队副本", 114, 24)
raidTab:SetPoint("LEFT", dungeonTab, "RIGHT", 8, 0)

local searchBox = CreateFrame("EditBox", "OpenWoWEncounterJournalSearchBox", leftPanel,
    "InputBoxTemplate")
searchBox:SetSize(238, 24)
searchBox:SetPoint("TOPLEFT", dungeonTab, "BOTTOMLEFT", 2, -12)
searchBox:SetAutoFocus(false)
searchBox:SetMaxLetters(64)
searchBox:SetTextInsets(8, 8, 0, 0)

local searchHint = CreateLabel(leftPanel, "GameFontDisableSmall", "搜索副本或首领")
searchHint:SetPoint("LEFT", searchBox, "LEFT", 9, 0)
searchHint:SetJustifyH("LEFT")

local tierButtons = {}
local tierDefinitions = {
    { value = nil, text = "全部" },
    { value = 0, text = "经典" },
    { value = 1, text = "外域" },
    { value = 2, text = "诺森德" },
}
for index, definition in ipairs(tierDefinitions) do
    local button = CreateActionButton(leftPanel, definition.text, 57, 21)
    if index == 1 then
        button:SetPoint("TOPLEFT", searchBox, "BOTTOMLEFT", -2, -9)
    else
        button:SetPoint("LEFT", tierButtons[index - 1], "RIGHT", 4, 0)
    end
    button.tier = definition.value
    tierButtons[index] = button
end

local instanceList = CreateFrame("Frame", nil, leftPanel)
instanceList:SetPoint("TOPLEFT", tierButtons[1], "BOTTOMLEFT", 0, -9)
instanceList:SetPoint("BOTTOMRIGHT", leftPanel, "BOTTOMRIGHT", -12, 35)
instanceList:EnableMouseWheel(true)

local instanceRows = {}
for index = 1, 14 do
    local row = CreateListRow(instanceList, 236, 30)
    row:SetPoint("TOPLEFT", instanceList, "TOPLEFT", 0, -((index - 1) * 31))
    instanceRows[index] = row
end

local instancePrevious = CreateActionButton(leftPanel, "上移", 72, 20)
instancePrevious:SetPoint("BOTTOMLEFT", leftPanel, "BOTTOMLEFT", 13, 10)
local instanceCountLabel = CreateLabel(leftPanel, "GameFontDisableSmall", "")
instanceCountLabel:SetPoint("CENTER", leftPanel, "BOTTOM", 0, 20)
local instanceNext = CreateActionButton(leftPanel, "下移", 72, 20)
instanceNext:SetPoint("BOTTOMRIGHT", leftPanel, "BOTTOMRIGHT", -13, 10)

local contentPanel = CreateFrame("Frame", nil, frame)
contentPanel:SetPoint("TOPLEFT", leftPanel, "TOPRIGHT", 10, 0)
contentPanel:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -12, 12)
SetPanelBackdrop(contentPanel, 0.05, 0.041, 0.025, 0.96)

local instanceTitle = CreateLabel(contentPanel, "GameFontNormalLarge", "请选择一个副本")
instanceTitle:SetPoint("TOPLEFT", contentPanel, "TOPLEFT", 18, -16)
instanceTitle:SetPoint("RIGHT", contentPanel, "RIGHT", -18, 0)
instanceTitle:SetJustifyH("LEFT")

local instanceMeta = CreateLabel(contentPanel, "GameFontHighlightSmall", "")
instanceMeta:SetPoint("TOPLEFT", instanceTitle, "BOTTOMLEFT", 0, -5)
instanceMeta:SetTextColor(0.68, 0.68, 0.68)

local difficultyButtons = {}
for index = 1, 4 do
    local button = CreateActionButton(contentPanel, "", 98, 22)
    if index == 1 then
        button:SetPoint("TOPLEFT", instanceMeta, "BOTTOMLEFT", -2, -10)
    else
        button:SetPoint("LEFT", difficultyButtons[index - 1], "RIGHT", 6, 0)
    end
    difficultyButtons[index] = button
end

local divider = contentPanel:CreateTexture(nil, "ARTWORK")
divider:SetTexture(0.45, 0.36, 0.20, 0.65)
divider:SetPoint("TOPLEFT", contentPanel, "TOPLEFT", 18, -94)
divider:SetPoint("TOPRIGHT", contentPanel, "TOPRIGHT", -18, -94)
divider:SetHeight(1)

local bossPanel = CreateFrame("Frame", nil, contentPanel)
bossPanel:SetPoint("TOPLEFT", contentPanel, "TOPLEFT", 14, -106)
bossPanel:SetPoint("BOTTOMLEFT", contentPanel, "BOTTOMLEFT", 14, 14)
bossPanel:SetWidth(206)
SetPanelBackdrop(bossPanel, 0.035, 0.03, 0.022, 0.78)

local bossHeading = CreateLabel(bossPanel, "GameFontNormal", "首领")
bossHeading:SetPoint("TOPLEFT", bossPanel, "TOPLEFT", 10, -9)

local bossRows = {}
for index = 1, 12 do
    local row = CreateListRow(bossPanel, 184, 32)
    row:SetPoint("TOPLEFT", bossPanel, "TOPLEFT", 10, -(31 + ((index - 1) * 33)))
    row.icon = row:CreateTexture(nil, "ARTWORK")
    row.icon:SetSize(24, 24)
    row.icon:SetPoint("LEFT", row, "LEFT", 5, 0)
    row.text:ClearAllPoints()
    row.text:SetPoint("LEFT", row.icon, "RIGHT", 6, 0)
    row.text:SetPoint("RIGHT", row, "RIGHT", -5, 0)
    bossRows[index] = row
end
bossPanel:EnableMouseWheel(true)

local bossPrevious = CreateActionButton(bossPanel, "上移", 62, 19)
bossPrevious:SetPoint("BOTTOMLEFT", bossPanel, "BOTTOMLEFT", 10, 8)
local bossNext = CreateActionButton(bossPanel, "下移", 62, 19)
bossNext:SetPoint("BOTTOMRIGHT", bossPanel, "BOTTOMRIGHT", -10, 8)

local detailPanel = CreateFrame("Frame", nil, contentPanel)
detailPanel:SetPoint("TOPLEFT", bossPanel, "TOPRIGHT", 10, 0)
detailPanel:SetPoint("BOTTOMRIGHT", contentPanel, "BOTTOMRIGHT", -14, 14)
SetPanelBackdrop(detailPanel, 0.035, 0.03, 0.022, 0.78)

local bossIcon = detailPanel:CreateTexture(nil, "ARTWORK")
bossIcon:SetSize(48, 48)
bossIcon:SetPoint("TOPLEFT", detailPanel, "TOPLEFT", 13, -13)
bossIcon:SetTexture(UNKNOWN_ICON)

local bossTitle = CreateLabel(detailPanel, "GameFontNormalLarge", "选择一名首领")
bossTitle:SetPoint("TOPLEFT", bossIcon, "TOPRIGHT", 10, -3)
bossTitle:SetPoint("RIGHT", detailPanel, "RIGHT", -12, 0)
bossTitle:SetJustifyH("LEFT")

local bossSource = CreateLabel(detailPanel, "GameFontDisableSmall",
    "技能来自 build 12340 可验证目录")
bossSource:SetPoint("TOPLEFT", bossTitle, "BOTTOMLEFT", 0, -5)

local abilityTab = CreateActionButton(detailPanel, "技能与机制", 104, 22)
abilityTab:SetPoint("TOPLEFT", detailPanel, "TOPLEFT", 11, -73)
local lootTab = CreateActionButton(detailPanel, "首领掉落", 104, 22)
lootTab:SetPoint("LEFT", abilityTab, "RIGHT", 7, 0)

local abilityDisclaimer = CreateLabel(detailPanel, "GameFontDisableSmall",
    "展示可可靠联接的技能，不代表攻略中的完整机制清单。")
abilityDisclaimer:SetPoint("TOPLEFT", abilityTab, "BOTTOMLEFT", 2, -4)
abilityDisclaimer:SetPoint("RIGHT", detailPanel, "RIGHT", -12, 0)
abilityDisclaimer:SetJustifyH("LEFT")

local abilityList = CreateFrame("Frame", nil, detailPanel)
abilityList:SetPoint("TOPLEFT", detailPanel, "TOPLEFT", 10, -116)
abilityList:SetPoint("BOTTOMRIGHT", detailPanel, "BOTTOMRIGHT", -10, 36)
abilityList:EnableMouseWheel(true)

local abilityRows = {}
for index = 1, 4 do
    local row = CreateFrame("Button", nil, abilityList)
    row:SetSize(384, 78)
    row:SetPoint("TOPLEFT", abilityList, "TOPLEFT", 0, -((index - 1) * 80))
    row.background = row:CreateTexture(nil, "BACKGROUND")
    row.background:SetAllPoints(row)
    row.background:SetTexture(0.075, 0.064, 0.045, 0.76)
    row.highlight = row:CreateTexture(nil, "HIGHLIGHT")
    row.highlight:SetAllPoints(row)
    row.highlight:SetTexture(0.30, 0.24, 0.12, 0.35)
    row.icon = row:CreateTexture(nil, "ARTWORK")
    row.icon:SetSize(38, 38)
    row.icon:SetPoint("TOPLEFT", row, "TOPLEFT", 7, -7)
    row.title = CreateLabel(row, "GameFontNormal", "")
    row.title:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 8, -1)
    row.title:SetPoint("RIGHT", row, "RIGHT", -7, 0)
    row.title:SetJustifyH("LEFT")
    row.description = CreateLabel(row, "GameFontHighlightSmall", "")
    row.description:SetPoint("TOPLEFT", row.title, "BOTTOMLEFT", 0, -4)
    row.description:SetPoint("BOTTOMRIGHT", row, "BOTTOMRIGHT", -7, 6)
    row.description:SetJustifyH("LEFT")
    row.description:SetJustifyV("TOP")
    row.description:SetWordWrap(true)
    abilityRows[index] = row
end

local abilityPrevious = CreateActionButton(detailPanel, "上一页", 72, 20)
abilityPrevious:SetPoint("BOTTOMLEFT", detailPanel, "BOTTOMLEFT", 11, 9)
local abilityCountLabel = CreateLabel(detailPanel, "GameFontDisableSmall", "")
abilityCountLabel:SetPoint("CENTER", detailPanel, "BOTTOM", 0, 19)
local abilityNext = CreateActionButton(detailPanel, "下一页", 72, 20)
abilityNext:SetPoint("BOTTOMRIGHT", detailPanel, "BOTTOMRIGHT", -11, 9)

local emptyState = CreateLabel(detailPanel, "GameFontHighlight", "")
emptyState:SetPoint("CENTER", abilityList, "CENTER", 0, 15)
emptyState:SetWidth(340)
emptyState:SetJustifyH("CENTER")

local errorPanel = CreateFrame("Frame", nil, frame)
errorPanel:SetAllPoints(contentPanel)
SetPanelBackdrop(errorPanel, 0.08, 0.025, 0.02, 0.98)
errorPanel:Hide()
local errorTitle = CreateLabel(errorPanel, "GameFontNormalLarge", "地下城手册暂不可用")
errorTitle:SetPoint("CENTER", errorPanel, "CENTER", 0, 24)
local errorText = CreateLabel(errorPanel, "GameFontHighlight", "")
errorText:SetPoint("TOP", errorTitle, "BOTTOM", 0, -12)
errorText:SetWidth(500)
errorText:SetJustifyH("CENTER")

local RefreshInstances
local RefreshInstanceRows
local RefreshBosses
local RefreshBossRows
local RefreshAbilities
local RefreshAbilityRows
local SelectInstance
local SelectBoss
local SelectDifficulty

local function InstanceMatchesSearch(instance, query)
    if ContainsText(instance.name, query) or ContainsText(instance.description, query) then
        return true
    end
    local difficultyCount = API.GetNumDifficulties(instance.mapID)
    for difficultyIndex = 1, difficultyCount do
        local difficulty = API.GetDifficultyByIndex(instance.mapID, difficultyIndex)
        if difficulty ~= nil then
            local encounterCount = API.GetNumEncounters(instance.mapID, difficulty)
            for encounterIndex = 1, encounterCount do
                local encounterID, name = API.GetEncounterByIndex(
                    instance.mapID, difficulty, encounterIndex)
                if encounterID and ContainsText(name, query) then
                    return true
                end
            end
        end
    end
    return false
end

RefreshInstanceRows = function()
    local visibleCount = #instanceRows
    local maxOffset = math.max(0, #state.instances - visibleCount)
    state.instanceOffset = math.min(math.max(0, state.instanceOffset), maxOffset)

    for index, row in ipairs(instanceRows) do
        local instance = state.instances[state.instanceOffset + index]
        row.instance = instance
        if instance then
            row.text:SetText(instance.name)
            if state.instance and state.instance.mapID == instance.mapID then
                row.background:SetTexture(0.35, 0.24, 0.075, 0.90)
                row.text:SetTextColor(1.0, 0.82, 0.0)
            else
                row.background:SetTexture(0.10, 0.085, 0.06, 0.78)
                row.text:SetTextColor(0.95, 0.95, 0.95)
            end
            row:Show()
        else
            row:Hide()
        end
    end
    instanceCountLabel:SetText(#state.instances .. " 个副本")
end

RefreshInstances = function(preserveSelection)
    local selectedMapID = preserveSelection and state.instance and state.instance.mapID or nil
    state.instances = {}
    local query = string.lower(state.search or "")
    local count = API.GetNumInstances(state.isRaid)
    for index = 1, count do
        local mapID, name, description, texture, minLevel, maxLevel, expansion,
            isRaid, lfgID = API.GetInstanceByIndex(index, state.isRaid)
        if mapID then
            local instance = {
                mapID = mapID,
                name = name or ("地图 " .. mapID),
                description = description or "",
                texture = texture,
                minLevel = minLevel or 0,
                maxLevel = maxLevel or 0,
                expansion = expansion or 0,
                isRaid = isRaid and true or false,
                lfgID = lfgID,
            }
            if (state.tier == nil or instance.expansion == state.tier) and
                InstanceMatchesSearch(instance, query) then
                table.insert(state.instances, instance)
            end
        end
    end

    state.instanceOffset = 0
    local preserved = nil
    if selectedMapID then
        for _, instance in ipairs(state.instances) do
            if instance.mapID == selectedMapID then
                preserved = instance
                break
            end
        end
    end
    RefreshInstanceRows()
    if preserved then
        SelectInstance(preserved)
    elseif #state.instances > 0 then
        SelectInstance(state.instances[1])
    else
        state.instance = nil
        state.boss = nil
        state.bosses = {}
        state.abilities = {}
        state.loot = {}
        instanceTitle:SetText("没有匹配的副本")
        instanceMeta:SetText("请调整资料片筛选或搜索内容。")
        RefreshBossRows()
        RefreshAbilityRows()
    end
end

RefreshBossRows = function()
    local visibleCount = #bossRows
    local maxOffset = math.max(0, #state.bosses - visibleCount)
    state.bossOffset = math.min(math.max(0, state.bossOffset), maxOffset)
    for index, row in ipairs(bossRows) do
        local boss = state.bosses[state.bossOffset + index]
        row.boss = boss
        if boss then
            row.text:SetText(boss.name)
            row.icon:SetTexture(boss.icon or UNKNOWN_ICON)
            if state.boss and state.boss.id == boss.id then
                row.background:SetTexture(0.35, 0.24, 0.075, 0.90)
                row.text:SetTextColor(1.0, 0.82, 0.0)
            else
                row.background:SetTexture(0.10, 0.085, 0.06, 0.78)
                row.text:SetTextColor(0.95, 0.95, 0.95)
            end
            row:Show()
        else
            row:Hide()
        end
    end
end

RefreshBosses = function()
    state.bosses = {}
    state.bossOffset = 0
    state.boss = nil
    if not state.instance or state.difficulty == nil then
        RefreshBossRows()
        RefreshAbilities()
        return
    end
    local count = API.GetNumEncounters(state.instance.mapID, state.difficulty)
    for index = 1, count do
        local encounterID, name, icon, orderIndex, creatureEntry =
            API.GetEncounterByIndex(state.instance.mapID, state.difficulty, index)
        if encounterID then
            table.insert(state.bosses, {
                id = encounterID,
                name = name or ("首领 " .. encounterID),
                icon = icon,
                orderIndex = orderIndex or index,
                creatureEntry = creatureEntry,
            })
        end
    end
    RefreshBossRows()
    if #state.bosses > 0 then
        SelectBoss(state.bosses[1])
    else
        RefreshAbilities()
    end
end

RefreshAbilityRows = function()
    local pageSize = #abilityRows
    local entries = state.detailMode == "loot" and state.loot or state.abilities
    local maxOffset = math.max(0, #entries - pageSize)
    state.abilityOffset = math.min(math.max(0, state.abilityOffset), maxOffset)
    for index, row in ipairs(abilityRows) do
        local entry = entries[state.abilityOffset + index]
        row.ability = state.detailMode == "abilities" and entry or nil
        row.item = state.detailMode == "loot" and entry or nil
        if entry then
            row.icon:SetTexture(entry.icon or UNKNOWN_ICON)
            row.title:SetText(entry.name)
            if state.detailMode == "loot" then
                local color = QUALITY_COLORS[entry.quality] or QUALITY_COLORS[1]
                row.title:SetTextColor(color[1], color[2], color[3])
                local detail = "物品等级 " .. entry.itemLevel
                if entry.requiredLevel and entry.requiredLevel > 0 then
                    detail = detail .. " · 需要等级 " .. entry.requiredLevel
                end
                row.description:SetText(detail .. "\n物品 ID：" .. entry.id)
            else
                row.title:SetTextColor(1.0, 0.82, 0.0)
                local summary = entry.description
                if summary == "" then
                    summary = entry.tooltip
                end
                if summary == "" then
                    summary = "该技能在 build 12340 数据中没有可展示的说明。"
                end
                row.description:SetText(summary)
            end
            row:Show()
        else
            row:Hide()
        end
    end

    if not state.boss then
        emptyState:SetText("请从左侧选择一名首领。")
        emptyState:Show()
    elseif #entries == 0 and state.detailMode == "loot" then
        emptyState:SetText("该首领在当前难度下没有能够通过 build 12340 物品数据校验的掉落条目。")
        emptyState:Show()
    elseif #entries == 0 then
        emptyState:SetText("该首领没有能够同时通过目录映射与 build 12340 Spell.dbc 校验的技能条目。")
        emptyState:Show()
    else
        emptyState:Hide()
    end

    if #entries == 0 then
        abilityCountLabel:SetText("")
    else
        local first = state.abilityOffset + 1
        local last = math.min(#entries, state.abilityOffset + pageSize)
        abilityCountLabel:SetText(first .. "–" .. last .. " / " .. #entries)
    end
end

RefreshAbilities = function()
    state.abilities = {}
    state.loot = {}
    state.abilityOffset = 0
    if state.boss then
        local count = API.GetNumAbilities(state.boss.id)
        for index = 1, count do
            local spellID, name, description, tooltip, icon, verifiedBuild =
                API.GetAbilityByIndex(state.boss.id, index)
            if spellID then
                table.insert(state.abilities, {
                    id = spellID,
                    name = name or ("法术 " .. spellID),
                    description = description or "",
                    tooltip = tooltip or "",
                    icon = icon,
                    verifiedBuild = verifiedBuild,
                })
            end
        end
        local lootCount = API.GetNumLoot(state.boss.id, state.difficulty)
        for index = 1, lootCount do
            local itemID, name, icon, quality, itemLevel, requiredLevel,
                inventoryType = API.GetLootByIndex(
                    state.boss.id, state.difficulty, index)
            if itemID then
                table.insert(state.loot, {
                    id = itemID,
                    name = name or ("物品 " .. itemID),
                    icon = icon,
                    quality = quality or 1,
                    itemLevel = itemLevel or 0,
                    requiredLevel = requiredLevel or 0,
                    inventoryType = inventoryType or 0,
                })
            end
        end
    end
    RefreshAbilityRows()
end

SelectBoss = function(boss)
    state.boss = boss
    bossTitle:SetText(boss and boss.name or "选择一名首领")
    bossIcon:SetTexture(boss and boss.icon or UNKNOWN_ICON)
    if boss and boss.creatureEntry then
        bossSource:SetText("首领实体 " .. boss.creatureEntry .. " · build 12340 可验证技能")
    else
        bossSource:SetText("build 12340 可验证技能")
    end
    RefreshBossRows()
    RefreshAbilities()
end

SelectDifficulty = function(difficulty)
    state.difficulty = difficulty
    for _, button in ipairs(difficultyButtons) do
        if button.difficulty ~= nil then
            if button.difficulty == difficulty then
                button:Disable()
            else
                button:Enable()
            end
        end
    end
    RefreshBosses()
end

SelectInstance = function(instance)
    state.instance = instance
    state.difficulties = {}
    state.difficulty = nil
    instanceTitle:SetText(instance.name)
    local levelText = "等级 " .. instance.minLevel .. "–" .. instance.maxLevel
    instanceMeta:SetText(ExpansionName(instance.expansion) .. " · " .. levelText)

    local count = API.GetNumDifficulties(instance.mapID)
    for index = 1, count do
        local rawDifficulty, maxPlayers, difficultyString =
            API.GetDifficultyByIndex(instance.mapID, index)
        if rawDifficulty ~= nil then
            table.insert(state.difficulties, {
                raw = rawDifficulty,
                maxPlayers = maxPlayers,
                difficultyString = difficultyString,
            })
        end
    end

    for index, button in ipairs(difficultyButtons) do
        local difficulty = state.difficulties[index]
        button.difficulty = difficulty and difficulty.raw or nil
        if difficulty then
            button:SetText(DifficultyName(difficulty.raw, difficulty.maxPlayers,
                instance.isRaid))
            button:Show()
        else
            button:Hide()
        end
    end
    RefreshInstanceRows()
    if #state.difficulties > 0 then
        SelectDifficulty(state.difficulties[1].raw)
    else
        RefreshBosses()
    end
end

local function SetInstanceOffset(offset)
    state.instanceOffset = offset
    RefreshInstanceRows()
end

local function SetBossOffset(offset)
    state.bossOffset = offset
    RefreshBossRows()
end

local function SetAbilityOffset(offset)
    state.abilityOffset = offset
    RefreshAbilityRows()
end

local function SetDetailMode(mode)
    state.detailMode = mode
    state.abilityOffset = 0
    if mode == "loot" then
        lootTab:Disable()
        abilityTab:Enable()
        abilityDisclaimer:SetText("仅展示稀有及以上、可归属到当前首领与难度的掉落。")
    else
        abilityTab:Disable()
        lootTab:Enable()
        abilityDisclaimer:SetText("展示可可靠联接的技能，不代表攻略中的完整机制清单。")
    end
    RefreshAbilityRows()
end

dungeonTab:SetScript("OnClick", function()
    if state.isRaid then
        state.isRaid = false
        OpenWoWEncounterJournalDB.isRaid = false
        RefreshInstances(false)
    end
end)
raidTab:SetScript("OnClick", function()
    if not state.isRaid then
        state.isRaid = true
        OpenWoWEncounterJournalDB.isRaid = true
        RefreshInstances(false)
    end
end)

searchBox:SetScript("OnTextChanged", function(self)
    state.search = self:GetText() or ""
    if state.search == "" then
        searchHint:Show()
    else
        searchHint:Hide()
    end
    RefreshInstances(true)
end)
searchBox:SetScript("OnEscapePressed", function(self)
    self:SetText("")
    self:ClearFocus()
end)
searchBox:SetScript("OnEnterPressed", function(self)
    self:ClearFocus()
end)

for _, button in ipairs(tierButtons) do
    button:SetScript("OnClick", function(self)
        state.tier = self.tier
        OpenWoWEncounterJournalDB.tier = self.tier
        RefreshInstances(false)
        for _, other in ipairs(tierButtons) do
            if other.tier == state.tier then
                other:Disable()
            else
                other:Enable()
            end
        end
    end)
end

for _, row in ipairs(instanceRows) do
    row:SetScript("OnClick", function(self)
        if self.instance then
            SelectInstance(self.instance)
        end
    end)
end
for _, row in ipairs(bossRows) do
    row:SetScript("OnClick", function(self)
        if self.boss then
            SelectBoss(self.boss)
        end
    end)
end
for _, row in ipairs(abilityRows) do
    row:SetScript("OnEnter", function(self)
        if not self.ability and not self.item then
            return
        end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        if self.item then
            local cachedName, cachedLink = GetItemInfo(self.item.id)
            if cachedName and cachedLink and GameTooltip.SetHyperlink then
                GameTooltip:SetHyperlink(cachedLink)
            else
                local color = QUALITY_COLORS[self.item.quality] or QUALITY_COLORS[1]
                GameTooltip:SetText(self.item.name, color[1], color[2], color[3])
                GameTooltip:AddLine("物品等级 " .. self.item.itemLevel, 1.0, 1.0, 1.0)
                if self.item.requiredLevel and self.item.requiredLevel > 0 then
                    GameTooltip:AddLine("需要等级 " .. self.item.requiredLevel,
                        0.75, 0.75, 0.75)
                end
                GameTooltip:AddLine("物品 ID：" .. self.item.id, 0.55, 0.55, 0.55)
                GameTooltip:AddLine("物品详细属性正在从服务器缓存获取。",
                    0.55, 0.55, 0.55, true)
            end
        else
            GameTooltip:SetText(self.ability.name, 1.0, 0.82, 0.0)
            local description = self.ability.description
            if description == "" then
                description = self.ability.tooltip
            end
            if description ~= "" then
                GameTooltip:AddLine(description, 1.0, 1.0, 1.0, true)
            end
            GameTooltip:AddLine("法术 ID：" .. self.ability.id, 0.55, 0.55, 0.55)
        end
        GameTooltip:Show()
    end)
    row:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
end
abilityTab:SetScript("OnClick", function()
    SetDetailMode("abilities")
end)
lootTab:SetScript("OnClick", function()
    SetDetailMode("loot")
end)
for _, button in ipairs(difficultyButtons) do
    button:SetScript("OnClick", function(self)
        if self.difficulty ~= nil then
            SelectDifficulty(self.difficulty)
        end
    end)
end

instancePrevious:SetScript("OnClick", function()
    SetInstanceOffset(state.instanceOffset - #instanceRows)
end)
instanceNext:SetScript("OnClick", function()
    SetInstanceOffset(state.instanceOffset + #instanceRows)
end)
instanceList:SetScript("OnMouseWheel", function(_, delta)
    SetInstanceOffset(state.instanceOffset - delta * 3)
end)
bossPrevious:SetScript("OnClick", function()
    SetBossOffset(state.bossOffset - #bossRows)
end)
bossNext:SetScript("OnClick", function()
    SetBossOffset(state.bossOffset + #bossRows)
end)
bossPanel:SetScript("OnMouseWheel", function(_, delta)
    SetBossOffset(state.bossOffset - delta * 3)
end)
abilityPrevious:SetScript("OnClick", function()
    SetAbilityOffset(state.abilityOffset - #abilityRows)
end)
abilityNext:SetScript("OnClick", function()
    SetAbilityOffset(state.abilityOffset + #abilityRows)
end)
abilityList:SetScript("OnMouseWheel", function(_, delta)
    SetAbilityOffset(state.abilityOffset - delta * #abilityRows)
end)

frame:SetScript("OnShow", function()
    if not API or type(API.GetSchemaVersion) ~= "function" then
        errorPanel:Show()
        errorText:SetText("原生目录服务 C_OpenWoWJournal 未注册。请确认正在运行包含地下城手册支持的 OpenWoW 构建。")
        return
    end
    local schema = API.GetSchemaVersion()
    if schema < REQUIRED_SCHEMA then
        errorPanel:Show()
        errorText:SetText("目录服务版本过旧：需要 " .. REQUIRED_SCHEMA .. "，当前为 " .. schema .. "。")
        return
    end
    errorPanel:Hide()
    if state.isRaid then
        raidTab:Disable()
        dungeonTab:Enable()
    else
        dungeonTab:Disable()
        raidTab:Enable()
    end
    for _, button in ipairs(tierButtons) do
        if button.tier == state.tier then
            button:Disable()
        else
            button:Enable()
        end
    end
    SetDetailMode(state.detailMode)
    RefreshInstances(true)
end)

frame:SetScript("OnHide", function()
    searchBox:ClearFocus()
    GameTooltip:Hide()
end)

frame:RegisterEvent("ADDON_LOADED")
frame:SetScript("OnEvent", function(self, event, addonName)
    if event ~= "ADDON_LOADED" or addonName ~= "OpenWoW_EncounterJournal" then
        return
    end
    OpenWoWEncounterJournalDB = OpenWoWEncounterJournalDB or {}
    state.isRaid = OpenWoWEncounterJournalDB.isRaid and true or false
    state.tier = OpenWoWEncounterJournalDB.tier
    self:UnregisterEvent("ADDON_LOADED")
end)

table.insert(UISpecialFrames, frame:GetName())

function OpenWoWEncounterJournal_Toggle()
    if frame:IsShown() then
        frame:Hide()
    else
        frame:Show()
    end
end
