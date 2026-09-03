-- Compatibility boundary for Blizzard_EncounterJournal 4.3.4.
-- The presentation files remain the original Cataclysm Lua/XML; this file
-- supplies the later FrameXML helpers and EJ_* query contract from build
-- 12340 data exposed by C_OpenWoWJournal.

local API = C_OpenWoWJournal
local REQUIRED_SCHEMA = 7
local PRESENTATION = OpenWoW_EncounterJournal_Presentation

if type(API) ~= "table" or type(API.GetSchemaVersion) ~= "function" then
    error("OpenWoW Encounter Journal: native catalog API is unavailable")
end
if API.GetSchemaVersion() < REQUIRED_SCHEMA then
    error("OpenWoW Encounter Journal: native catalog schema is too old")
end
if type(PRESENTATION) ~= "table" or PRESENTATION.schema ~= 1 or
    PRESENTATION.targetBuild ~= 12340 or PRESENTATION.locale ~= "zhCN" then
    error("OpenWoW Encounter Journal: presentation supplement is unavailable or incompatible")
end

local DEFAULT_BACKGROUND = "Interface\\EncounterJournal\\UI-EJ-BACKGROUND-Default"
local DEFAULT_BOSS_IMAGE = "Interface\\EncounterJournal\\UI-EJ-BOSS-Default"
local DEFAULT_BUTTON_IMAGE = "Interface\\EncounterJournal\\UI-EJ-DUNGEONBUTTON-Default"
local DEFAULT_LORE_IMAGE = "Interface\\EncounterJournal\\UI-EJ-LOREBG-Default"
local DEFAULT_SEARCH_CREATURE = "Interface\\EncounterJournal\\UI-EJ-GenericSearchCreature"
local UNKNOWN_ICON = "Interface\\Icons\\INV_Misc_QuestionMark"

local function InstallString(name, value)
    if _G[name] == nil then
        _G[name] = value
    end
end

InstallString("ENCOUNTER_JOURNAL", "地下城手册")
InstallString("ENCOUNTER_JOURNAL_ABILITY", "能力")
InstallString("ENCOUNTER_JOURNAL_DIFF_TEXT", "(%d) %s")
InstallString("ENCOUNTER_JOURNAL_ENCOUNTER", "首领")
InstallString("ENCOUNTER_JOURNAL_ENCOUNTER_ADD", "小怪")
InstallString("ENCOUNTER_JOURNAL_INSTANCE", "地下城")
InstallString("ENCOUNTER_JOURNAL_ITEM", "物品")
InstallString("ENCOUNTER_JOURNAL_SEARCH_RESULTS", "搜索结果\"%s\"(%d)")
InstallString("ENCOUNTER_JOURNAL_SHOW_MAP", "显示\n地图")
InstallString("ENCOUNTER_JOURNAL_SHOW_SEARCH_RESULTS", "显示全部%d个结果")
InstallString("EJ_CLASS_FILTER", "职业筛选：%s")
InstallString("BOSS_INFO_STRING", "首领：%s")
InstallString("PLAYER_DIFFICULTY1", "普通")
InstallString("PLAYER_DIFFICULTY2", "英雄")
InstallString("PLAYER_DIFFICULTY3", "团队查找器")

local sectionFlags = {
    "坦克预警", "伤害输出预警", "治疗预警", "英雄难度", "灭团技",
    "重要", "可打断技能", "法术效果", "诅咒效果", "毒药效果",
    "疾病效果", "激怒", "激怒",
}
for index, text in ipairs(sectionFlags) do
    InstallString("ENCOUNTER_JOURNAL_SECTION_FLAG" .. (index - 1), text)
    InstallString("ENCOUNTER_JOURNAL_SECTION_FLAG_DESCRIPTION" .. (index - 1), "")
end

function SearchBoxTemplate_OnLoad(self)
    self:SetText(SEARCH)
    self:SetFontObject("GameFontDisable")
    self.searchIcon:SetVertexColor(0.6, 0.6, 0.6)
    self:SetTextInsets(16, 20, 0, 0)
end

function SearchBoxTemplate_OnEditFocusLost(self)
    self:HighlightText(0, 0)
    self:SetFontObject("GameFontDisable")
    self.searchIcon:SetVertexColor(0.6, 0.6, 0.6)
    if self:GetText() == "" or self:GetText() == SEARCH then
        self:SetText(SEARCH)
        self.clearButton:Hide()
    end
end

function SerachBoxTemplate_OnEditFocusGained(self)
    self:HighlightText()
    self:SetFontObject("ChatFontSmall")
    self.searchIcon:SetVertexColor(1.0, 1.0, 1.0)
    if self:GetText() == SEARCH then
        self:SetText("")
    end
    self.clearButton:Show()
end

local state = {
    instancesBuilt = false,
    instances = {},
    instancesByID = {},
    encountersByID = {},
    sectionsByID = {},
    lootByItemID = {},
    encounterCache = {},
    lootCache = {},
    selectedInstance = nil,
    selectedEncounter = nil,
    difficulty = 1,
    tier = 3,
    classFilter = 0,
    searchCatalog = nil,
    searchResults = {},
}

for tier = 1, 3 do
    state.instances[tier] = { [false] = {}, [true] = {} }
end

local function IsNonEmpty(value)
    return type(value) == "string" and value ~= ""
end

local function JournalLink(linkType, id, difficulty, name)
    return string.format("|cff66bbff|Hjournal:%d:%d:%d|h[%s]|h|r",
        linkType, id or 0, difficulty or 1, name or "")
end

local function NormalizePresentationName(value)
    value = string.lower(tostring(value or ""))
    value = string.gsub(value, "[%s%p·‘’“”《》【】（）：—－]+", "")
    return value
end

local function ResolveJournalDifficultyText(value, difficulty)
    value = tostring(value or "")
    return string.gsub(value, "%$%[([^%]]+)%]([%s%S]-)%$%]", function(list, body)
        for token in string.gmatch(list, "%d+") do
            if tonumber(token) == difficulty then
                return body
            end
        end
        return ""
    end)
end

local function ExpandPresentationText(value, mapID, encounterID, sectionID, difficulty)
    if not IsNonEmpty(value) then
        return ""
    end
    value = ResolveJournalDifficultyText(value, difficulty or state.difficulty)
    local expanded = API.ExpandPresentationText(value, mapID or 0,
        encounterID or 0, sectionID or 0)
    return expanded or value
end

local function FindPresentationEncounter(instance, name, orderIndex)
    local presentation = instance and instance.presentation
    if not presentation then
        return nil
    end
    local normalized = NormalizePresentationName(name)
    local exact
    for _, candidate in ipairs(presentation.encounters or {}) do
        if candidate.name == name or candidate.matchName == normalized then
            if not exact or candidate.order == orderIndex then
                exact = candidate
            end
        end
    end
    return exact
end

local function BuildInstances()
    if state.instancesBuilt then
        return
    end
    for _, isRaid in ipairs({ false, true }) do
        local count = API.GetNumInstances(isRaid)
        for index = 1, count do
            local mapID, name, description, texture, minLevel, maxLevel, tier,
                raid, lfgID, worldMapAreaID =
                API.GetInstanceByIndex(index, isRaid)
            if mapID then
                local presentation = PRESENTATION.instances[mapID]
                local uiTier = (tonumber(tier) or 0) + 1
                local instance = {
                    id = mapID,
                    name = name or "",
                    description = presentation and IsNonEmpty(presentation.description) and
                        presentation.description or (description or ""),
                    buttonImage = presentation and IsNonEmpty(presentation.art.button) and
                        presentation.art.button or
                        (IsNonEmpty(texture) and texture or DEFAULT_BUTTON_IMAGE),
                    buttonSmallImage = presentation and IsNonEmpty(presentation.art.buttonSmall) and
                        presentation.art.buttonSmall or DEFAULT_BUTTON_IMAGE,
                    background = presentation and IsNonEmpty(presentation.art.background) and
                        presentation.art.background or DEFAULT_BACKGROUND,
                    loreImage = presentation and IsNonEmpty(presentation.art.lore) and
                        presentation.art.lore or DEFAULT_LORE_IMAGE,
                    minLevel = minLevel,
                    maxLevel = maxLevel,
                    tier = uiTier,
                    isRaid = raid and true or false,
                    lfgID = lfgID,
                    worldMapAreaID = worldMapAreaID,
                    presentation = presentation,
                }
                instance.link = JournalLink(0, mapID, 1, instance.name)
                if state.instances[uiTier] then
                    table.insert(state.instances[uiTier][isRaid], instance)
                end
                state.instancesByID[mapID] = instance
            end
        end
    end
    state.instancesBuilt = true
end

local function GetInstance(instanceID)
    BuildInstances()
    return state.instancesByID[instanceID or (state.selectedInstance and state.selectedInstance.id)]
end

local function RawDifficulty(uiDifficulty)
    if not uiDifficulty or uiDifficulty < 1 or uiDifficulty > 4 then
        return nil
    end
    return uiDifficulty - 1
end

local function IsDifficultyAvailable(instance, uiDifficulty)
    if not instance then
        return false
    end
    local expected = RawDifficulty(uiDifficulty)
    if expected == nil then
        return false
    end
    local count = API.GetNumDifficulties(instance.id)
    for index = 1, count do
        local raw = API.GetDifficultyByIndex(instance.id, index)
        if raw == expected then
            return true
        end
    end
    return false
end

local function FirstDifficulty(instance)
    if not instance then
        return 1
    end
    local raw = API.GetDifficultyByIndex(instance.id, 1)
    if raw and raw >= 0 and raw <= 3 then
        return raw + 1
    end
    return 1
end

local function EncounterKey(instanceID, rawDifficulty)
    return tostring(instanceID) .. ":" .. tostring(rawDifficulty)
end

local function BuildEncounters(instance, uiDifficulty)
    if not instance then
        return {}
    end
    local raw = RawDifficulty(uiDifficulty)
    if raw == nil then
        return {}
    end
    local key = EncounterKey(instance.id, raw)
    if state.encounterCache[key] then
        return state.encounterCache[key]
    end

    local encounters = {}
    local count = API.GetNumEncounters(instance.id, raw)
    for index = 1, count do
        local encounterID, name, icon, orderIndex, creatureEntry, creatureDisplayID =
            API.GetEncounterByIndex(instance.id, raw, index)
        if encounterID then
            local encounter = {
                id = encounterID,
                name = name or "",
                description = "",
                icon = icon,
                orderIndex = orderIndex,
                creatureEntry = creatureEntry,
                creatureDisplayID = creatureDisplayID,
                instanceID = instance.id,
                difficulty = uiDifficulty,
                rawDifficulty = raw,
                rootSectionID = nil,
            }
            encounter.link = JournalLink(1, encounterID, uiDifficulty, encounter.name)

            local presentationEncounter = FindPresentationEncounter(
                instance, encounter.name, encounter.orderIndex)
            if presentationEncounter then
                encounter.description = presentationEncounter.description or ""
                local donorToSection = {}
                for donorIndex, donor in ipairs(presentationEncounter.sections or {}) do
                    donorToSection[donor.id] = 10000000 + encounterID * 4096 + donorIndex
                end
                for _, donor in ipairs(presentationEncounter.sections or {}) do
                    local sectionID = donorToSection[donor.id]
                    local section = {
                        id = sectionID,
                        donorID = donor.id,
                        spellID = donor.spell or 0,
                        title = donor.title or "",
                        description = ExpandPresentationText(donor.body, instance.id,
                            encounterID, donor.id, uiDifficulty),
                        icon = IsNonEmpty(donor.icon) and donor.icon or UNKNOWN_ICON,
                        displayInfo = 0,
                        siblingID = donorToSection[donor.sibling],
                        childID = donorToSection[donor.child],
                        parentID = donorToSection[donor.parent],
                        sectionType = donor.type or 0,
                        flags = donor.flags or 0,
                        iconFlags = donor.iconFlags or 0,
                        difficultyMask = donor.difficultyMask,
                        encounterID = encounterID,
                        instanceID = instance.id,
                        difficulty = uiDifficulty,
                        source = instance.presentation.source,
                        sourceBuild = instance.presentation.build,
                    }
                    section.link = JournalLink(3, sectionID, uiDifficulty, section.title)
                    state.sectionsByID[sectionID] = section
                end
                encounter.rootSectionID = donorToSection[presentationEncounter.firstSection]
            else
                local previousSection
                local abilityCount = API.GetNumAbilities(encounterID)
                for abilityIndex = 1, abilityCount do
                    local spellID, abilityName, description, tooltip, abilityIcon, verifiedBuild =
                        API.GetAbilityByIndex(encounterID, abilityIndex)
                    if spellID then
                        local sectionID = 1000000 + encounterID * 16 + abilityIndex
                        local section = {
                            id = sectionID,
                            spellID = spellID,
                            title = abilityName or "",
                            description = IsNonEmpty(description) and description or (tooltip or ""),
                            icon = abilityIcon,
                            displayInfo = 0,
                            siblingID = nil,
                            childID = nil,
                            encounterID = encounterID,
                            instanceID = instance.id,
                            difficulty = uiDifficulty,
                            verifiedBuild = verifiedBuild,
                            iconFlags = 0,
                        }
                        section.link = JournalLink(3, sectionID, uiDifficulty, section.title)
                        state.sectionsByID[sectionID] = section
                        if previousSection then
                            previousSection.siblingID = sectionID
                        else
                            encounter.rootSectionID = sectionID
                        end
                        previousSection = section
                    end
                end
            end

            table.insert(encounters, encounter)
            if not state.encountersByID[encounterID] then
                state.encountersByID[encounterID] = encounter
            end
        end
    end
    state.encounterCache[key] = encounters
    return encounters
end

local function CurrentEncounters()
    return BuildEncounters(state.selectedInstance, state.difficulty)
end

local function FindEncounter(encounterID)
    if not encounterID and state.selectedEncounter then
        return state.selectedEncounter
    end
    for _, encounter in ipairs(CurrentEncounters()) do
        if encounter.id == encounterID then
            return encounter
        end
    end
    return state.encountersByID[encounterID]
end

local function LootKey(encounterID, rawDifficulty)
    return tostring(encounterID) .. ":" .. tostring(rawDifficulty)
end

local inventorySlots = {
    [0] = "", [1] = "头部", [2] = "颈部", [3] = "肩部", [4] = "衬衣",
    [5] = "胸部", [6] = "腰部", [7] = "腿部", [8] = "脚", [9] = "手腕",
    [10] = "手", [11] = "手指", [12] = "饰品", [13] = "单手",
    [14] = "盾牌", [15] = "远程", [16] = "背部", [17] = "双手",
    [18] = "容器", [19] = "战袍", [20] = "长袍", [21] = "主手",
    [22] = "副手", [23] = "副手物品", [24] = "弹药", [25] = "投掷",
    [26] = "远程", [28] = "圣物",
}

local itemQualityColors = {
    [0] = "ff9d9d9d", [1] = "ffffffff", [2] = "ff1eff00", [3] = "ff0070dd",
    [4] = "ffa335ee", [5] = "ffff8000", [6] = "ffe6cc80", [7] = "ff00ccff",
}

local function BuildLootForEncounter(encounter, uiDifficulty)
    if not encounter then
        return {}
    end
    local raw = RawDifficulty(uiDifficulty)
    if raw == nil then
        return {}
    end
    local key = LootKey(encounter.id, raw)
    if state.lootCache[key] then
        return state.lootCache[key]
    end

    local loot = {}
    local count = API.GetNumLoot(encounter.id, raw)
    for index = 1, count do
        local itemID, name, icon, quality, itemLevel, requiredLevel, inventoryType,
            allowableClass, armorType =
            API.GetLootByIndex(encounter.id, raw, index)
        if itemID then
            local color = itemQualityColors[quality] or itemQualityColors[1]
            local item = {
                id = itemID,
                name = name or "",
                icon = IsNonEmpty(icon) and icon or UNKNOWN_ICON,
                quality = quality or 1,
                itemLevel = itemLevel,
                requiredLevel = requiredLevel,
                inventoryType = inventoryType,
                allowableClass = allowableClass or -1,
                slot = inventorySlots[inventoryType] or "",
                armorType = IsNonEmpty(armorType) and armorType or "",
                encounterID = encounter.id,
                instanceID = encounter.instanceID,
                difficulty = uiDifficulty,
            }
            item.link = string.format("|c%s|Hitem:%d:0:0:0:0:0:0:0|h[%s]|h|r",
                color, itemID, item.name)
            table.insert(loot, item)
            if not state.lootByItemID[itemID] then
                state.lootByItemID[itemID] = item
            end
        end
    end
    state.lootCache[key] = loot
    return loot
end

local function CurrentLoot()
    if not state.selectedInstance then
        return {}
    end
    local loot = {}
    local encounters = state.selectedEncounter and { state.selectedEncounter }
        or CurrentEncounters()
    for _, encounter in ipairs(encounters) do
        for _, item in ipairs(BuildLootForEncounter(encounter, state.difficulty)) do
            local mask = item.allowableClass
            local classID = state.classFilter
            if classID == 0 or mask == -1 or
                (mask > 0 and math.floor(mask / (2 ^ (classID - 1))) % 2 == 1) then
                table.insert(loot, item)
            end
        end
    end
    return loot
end

function EJ_GetInstanceByIndex(index, isRaid)
    BuildInstances()
    local tierInstances = state.instances[state.tier]
    local instance = tierInstances and tierInstances[isRaid and true or false][index]
    if not instance then
        return nil
    end
    return instance.id, instance.name, instance.description, instance.background,
        instance.buttonImage, instance.loreImage, instance.worldMapAreaID,
        instance.link, true, nil, instance.id
end

function EJ_GetNumTiers()
    return 3
end

function EJ_GetTierInfo(tier)
    local record = PRESENTATION.tiers[tonumber(tier)]
    return record and record.name or nil
end

function EJ_GetCurrentTier()
    return state.tier
end

function EJ_SelectTier(tier)
    tier = tonumber(tier)
    if not tier or not state.instances[tier] then
        return
    end
    state.tier = tier
    if state.selectedInstance and state.selectedInstance.tier ~= tier then
        state.selectedInstance = nil
        state.selectedEncounter = nil
    end
end

function EJ_SelectInstance(instanceID)
    local instance = GetInstance(instanceID)
    state.selectedInstance = instance
    state.selectedEncounter = nil
    if instance then
        state.tier = instance.tier
    end
    if instance and not IsDifficultyAvailable(instance, state.difficulty) then
        state.difficulty = FirstDifficulty(instance)
    end
    if instance and EncounterJournal and EncounterJournal.encounter and
        EncounterJournal.encounter.info and
        EncounterJournal.encounter.info.difficulty then
        local size = 5
        if instance.isRaid then
            size = (state.difficulty == 2 or state.difficulty == 4) and 25 or 10
        end
        local difficultyName = (state.difficulty >= 3 or
            (not instance.isRaid and state.difficulty == 2)) and
            PLAYER_DIFFICULTY2 or PLAYER_DIFFICULTY1
        EncounterJournal.encounter.info.difficulty:SetFormattedText(
            ENCOUNTER_JOURNAL_DIFF_TEXT, size, difficultyName)
    end
end

function EJ_GetInstanceInfo(instanceID)
    local instance = GetInstance(instanceID)
    if not instance then
        return nil
    end
    return instance.name, instance.description, instance.background,
        instance.buttonImage, instance.loreImage, instance.worldMapAreaID,
        instance.link, true, nil, instance.id
end

function EJ_GetCurrentInstance()
    local currentName = GetInstanceInfo()
    if not IsNonEmpty(currentName) then
        return 0
    end
    BuildInstances()
    for instanceID, instance in pairs(state.instancesByID) do
        if instance.name == currentName then
            return instanceID
        end
    end
    return 0
end

function EJ_InstanceIsRaid()
    return state.selectedInstance and state.selectedInstance.isRaid or false
end

function EJ_GetDifficulty()
    return state.difficulty
end

function EJ_IsValidInstanceDifficulty(difficulty)
    return IsDifficultyAvailable(state.selectedInstance, difficulty)
end

function EJ_SetDifficulty(difficulty)
    difficulty = tonumber(difficulty)
    if not difficulty or difficulty < 1 then
        return
    end
    if state.selectedInstance and not IsDifficultyAvailable(state.selectedInstance, difficulty) then
        return
    end
    state.difficulty = difficulty
    if state.selectedEncounter then
        state.selectedEncounter = FindEncounter(state.selectedEncounter.id)
    end
    if EncounterJournal and EncounterJournal_OnEvent then
        EncounterJournal_OnEvent(EncounterJournal, "EJ_DIFFICULTY_UPDATE", difficulty)
    end
end

function EJ_GetEncounterInfoByIndex(index)
    local encounter = CurrentEncounters()[index]
    if not encounter then
        return nil
    end
    return encounter.name, encounter.description, encounter.id,
        encounter.rootSectionID, encounter.link
end

function EJ_GetEncounterInfo(encounterID)
    local encounter = FindEncounter(encounterID)
    if not encounter then
        return nil
    end
    return encounter.name, encounter.description, encounter.id,
        encounter.rootSectionID, encounter.link
end

function EJ_SelectEncounter(encounterID)
    state.selectedEncounter = FindEncounter(encounterID)
end

function EJ_GetCreatureInfo(index, encounterID)
    if index ~= 1 then
        return nil
    end
    local encounter = FindEncounter(encounterID)
    if not encounter then
        return nil
    end
    return encounter.creatureEntry or encounter.id, encounter.name,
        encounter.description, encounter.creatureDisplayID or 0,
        DEFAULT_BOSS_IMAGE
end

function EJ_GetSectionInfo(sectionID)
    local section = state.sectionsByID[sectionID]
    if not section then
        return nil
    end
    return section.title, section.description, 0, section.icon,
        section.displayInfo, section.siblingID, section.childID, false,
        section.link, false
end

function EJ_GetSectionIconFlags(sectionID)
    local section = state.sectionsByID[sectionID]
    return section and section.iconFlags or 0
end

function EJ_GetSectionPath(sectionID)
    if state.sectionsByID[sectionID] then
        return sectionID
    end
    return nil
end

function EJ_GetNumLoot()
    return #CurrentLoot()
end

local function LootReturn(item)
    if not item then
        return nil
    end
    return item.name, item.icon, item.slot, item.armorType, item.id,
        item.link, item.encounterID
end

function EJ_GetLootInfoByIndex(index)
    return LootReturn(CurrentLoot()[index])
end

local function BuildSearchCatalog()
    if state.searchCatalog then
        return
    end
    BuildInstances()
    local catalog = {}
    for tier = 1, EJ_GetNumTiers() do
        for _, isRaid in ipairs({ false, true }) do
            for _, instance in ipairs(state.instances[tier][isRaid]) do
            table.insert(catalog, {
                id = instance.id, searchType = 4, difficulty = 1,
                instanceID = instance.id, name = instance.name,
            })
            local difficultyCount = API.GetNumDifficulties(instance.id)
            local seenEncounters = {}
            for difficultyIndex = 1, difficultyCount do
                local raw = API.GetDifficultyByIndex(instance.id, difficultyIndex)
                if raw and raw >= 0 and raw <= 3 then
                    local uiDifficulty = raw + 1
                    for _, encounter in ipairs(BuildEncounters(instance, uiDifficulty)) do
                        if not seenEncounters[encounter.id] then
                            seenEncounters[encounter.id] = true
                            table.insert(catalog, {
                                id = encounter.id, searchType = 1,
                                difficulty = uiDifficulty, instanceID = instance.id,
                                encounterID = encounter.id, name = encounter.name,
                            })
                            local sectionID = encounter.rootSectionID
                            while sectionID do
                                local section = state.sectionsByID[sectionID]
                                if not section then
                                    break
                                end
                                table.insert(catalog, {
                                    id = section.id, searchType = 3,
                                    difficulty = uiDifficulty, instanceID = instance.id,
                                    encounterID = encounter.id, name = section.title,
                                })
                                sectionID = section.siblingID
                            end
                        end
                        for _, item in ipairs(BuildLootForEncounter(encounter, uiDifficulty)) do
                            table.insert(catalog, {
                                id = item.id, searchType = 0,
                                difficulty = uiDifficulty, instanceID = instance.id,
                                encounterID = encounter.id, name = item.name,
                            })
                        end
                    end
                end
            end
            end
        end
    end
    state.searchCatalog = catalog
end

function EJ_GetLootInfo(itemID)
    local item = state.lootByItemID[itemID]
    if not item then
        BuildSearchCatalog()
        item = state.lootByItemID[itemID]
    end
    return LootReturn(item)
end

function EJ_SetSearch(searchText)
    BuildSearchCatalog()
    local query = string.lower(tostring(searchText or ""))
    local results = {}
    if query ~= "" then
        for _, candidate in ipairs(state.searchCatalog) do
            if string.find(string.lower(candidate.name or ""), query, 1, true) then
                table.insert(results, candidate)
            end
        end
    end
    state.searchResults = results
end

function EJ_ClearSearch()
    state.searchResults = {}
end

function EJ_GetNumSearchResults()
    return #state.searchResults
end

function EJ_GetSearchResult(index)
    local result = state.searchResults[index]
    if not result then
        return nil
    end
    return result.id, result.searchType, result.difficulty,
        result.instanceID, result.encounterID
end

local classTokens = {
    [1] = "WARRIOR", [2] = "PALADIN", [3] = "HUNTER", [4] = "ROGUE",
    [5] = "PRIEST", [6] = "DEATHKNIGHT", [7] = "SHAMAN", [8] = "MAGE",
    [9] = "WARLOCK", [11] = "DRUID",
}
local classNames = {
    [1] = "战士", [2] = "圣骑士", [3] = "猎人", [4] = "潜行者", [5] = "牧师",
    [6] = "死亡骑士", [7] = "萨满祭司", [8] = "法师", [9] = "术士", [11] = "德鲁伊",
}
local availableClasses = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11 }

function EJ_GetAvailableClasses()
    return unpack(availableClasses)
end

function EJ_GetClassFilter()
    local classID = state.classFilter
    if classID == 0 then
        return 0, nil
    end
    local token = classTokens[classID]
    local localized = LOCALIZED_CLASS_NAMES_MALE and LOCALIZED_CLASS_NAMES_MALE[token]
    return classID, localized or classNames[classID]
end

function EJ_SetClassLootFilter(classID)
    state.classFilter = tonumber(classID) or 0
end

function EJ_HandleLinkPath(linkType, id)
    linkType = tonumber(linkType)
    id = tonumber(id)
    if linkType == 0 then
        local instance = GetInstance(id)
        return instance and instance.id or nil
    elseif linkType == 1 then
        if not state.encountersByID[id] then
            BuildSearchCatalog()
        end
        local encounter = state.encountersByID[id]
        return encounter and encounter.instanceID or nil,
            encounter and encounter.id or nil
    elseif linkType == 3 then
        if not state.sectionsByID[id] then
            BuildSearchCatalog()
        end
        local section = state.sectionsByID[id]
        return section and section.instanceID or nil,
            section and section.encounterID or nil,
            section and section.id or nil
    end
    return nil
end

function ToggleEncounterJournal()
    if not EncounterJournal then
        return
    end
    if EncounterJournal:IsShown() then
        HideUIPanel(EncounterJournal)
    else
        ShowUIPanel(EncounterJournal)
    end
end

function OpenWoWEncounterJournal_Toggle()
    ToggleEncounterJournal()
end

UIPanelWindows = UIPanelWindows or {}
UIPanelWindows.EncounterJournal = {
    area = "left", pushable = 0, whileDead = 1, width = 830,
}
UISpecialFrames = UISpecialFrames or {}
table.insert(UISpecialFrames, "EncounterJournal")
