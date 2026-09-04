local state = {
    layer = 1,
    wide = false,
    drawableWidth = 1,
    drawableHeight = 1,
    logicalWidth = 844,
    logicalHeight = 390,
    actionButtons = {},
    hiddenButtons = {},
    refreshElapsed = 0,
}

local locale = GetLocale and GetLocale() or "enUS"
local L
if locale == "zhCN" then
    L = {
        enemy = "敌人",
        friendly = "友方",
        interact = "交互",
        jump = "跳跃",
        drawer = "功能",
        character = "人物",
        spellbook = "法术",
        talents = "天赋",
        quest = "任务",
        map = "地图",
        bags = "背包",
        chat = "聊天",
        system = "系统",
    }
else
    L = {
        enemy = "Enemy",
        friendly = "Friend",
        interact = "Use",
        jump = "Jump",
        drawer = "Menu",
        character = "Hero",
        spellbook = "Spells",
        talents = "Talents",
        quest = "Quests",
        map = "Map",
        bags = "Bags",
        chat = "Chat",
        system = "System",
    }
end

local root = CreateFrame("Frame", "OpenWoWMobileRoot", UIParent)
root:SetAllPoints(UIParent)
root:SetFrameStrata("HIGH")

local actionCluster = CreateFrame("Frame", "OpenWoWMobileActionCluster", root)
actionCluster:SetFrameStrata("HIGH")

local function AddPanelBackground(frame, alpha)
    local background = frame:CreateTexture(nil, "BACKGROUND")
    background:SetAllPoints(frame)
    background:SetTexture(0.015, 0.02, 0.03, alpha or 0.72)
    frame.background = background
end

AddPanelBackground(actionCluster, 0.34)

local function AddButtonVisuals(button, iconPath)
    local background = button:CreateTexture(nil, "BACKGROUND")
    background:SetPoint("TOPLEFT", button, "TOPLEFT", 3, -3)
    background:SetPoint("BOTTOMRIGHT", button, "BOTTOMRIGHT", -3, 3)
    background:SetTexture(0.04, 0.055, 0.075, 0.92)
    button.background = background

    local icon = button:CreateTexture(nil, "ARTWORK")
    icon:SetPoint("TOPLEFT", button, "TOPLEFT", 6, -6)
    icon:SetPoint("BOTTOMRIGHT", button, "BOTTOMRIGHT", -6, 6)
    icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
    if iconPath then
        icon:SetTexture(iconPath)
    end
    button.icon = icon

    local border = button:CreateTexture(nil, "OVERLAY")
    border:SetAllPoints(button)
    border:SetTexture("Interface\\Buttons\\UI-Quickslot2")
    button.border = border

    button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square")
    button:SetPushedTexture("Interface\\Buttons\\UI-Quickslot-Depress")
    button:RegisterForClicks("LeftButtonUp")
    button:EnableMouse(true)
end

local function RunMobileBinding(binding)
    if not binding then
        return
    end
    RunBinding(binding, "down")
    RunBinding(binding, "up")
end

for index = 1, 12 do
    local button = CreateFrame("Button", "OpenWoWMobileAction" .. index, actionCluster)
    button.displayIndex = index
    AddButtonVisuals(button)

    local cooldown = CreateFrame("Cooldown", nil, button)
    cooldown:SetPoint("TOPLEFT", button.icon, "TOPLEFT", 0, 0)
    cooldown:SetPoint("BOTTOMRIGHT", button.icon, "BOTTOMRIGHT", 0, 0)
    button.cooldown = cooldown

    local count = button:CreateFontString(nil, "OVERLAY", "NumberFontNormalSmall")
    count:SetPoint("BOTTOMRIGHT", button, "BOTTOMRIGHT", -7, 7)
    count:SetJustifyH("RIGHT")
    button.count = count

    local slotLabel = button:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    slotLabel:SetPoint("TOPLEFT", button, "TOPLEFT", 8, -7)
    slotLabel:SetText(index)
    button.slotLabel = slotLabel

    button:SetScript("OnClick", function(self)
        RunMobileBinding("ACTIONBUTTON" .. self.actionIndex)
    end)
    state.actionButtons[index] = button
end

for index = 1, 6 do
    local button = CreateFrame("Button", "OpenWoWMobileHiddenAction" .. index, root)
    button.displayIndex = index
    AddButtonVisuals(button)
    button.border:SetVertexColor(0.62, 0.68, 0.78, 0.85)
    local cooldown = CreateFrame("Cooldown", nil, button)
    cooldown:SetPoint("TOPLEFT", button.icon, "TOPLEFT", 0, 0)
    cooldown:SetPoint("BOTTOMRIGHT", button.icon, "BOTTOMRIGHT", 0, 0)
    button.cooldown = cooldown
    button:EnableMouse(false)
    state.hiddenButtons[index] = button
end

local utilityRow = CreateFrame("Frame", "OpenWoWMobileUtilityRow", root)
utilityRow:SetFrameStrata("HIGH")

local utilityPanel = CreateFrame("Frame", "OpenWoWMobileUtilityPanel", root)
utilityPanel:SetFrameStrata("DIALOG")
AddPanelBackground(utilityPanel, 0.9)
utilityPanel:Hide()

local function CreateLabeledButton(name, parent, label, iconPath, callback)
    local button = CreateFrame("Button", name, parent)
    AddButtonVisuals(button, iconPath)
    local text = button:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    text:SetPoint("BOTTOM", button, "BOTTOM", 0, 8)
    text:SetText(label)
    text:SetTextColor(1.0, 0.88, 0.55)
    button.label = text
    button:SetScript("OnClick", callback)
    return button
end

local targetButton = CreateLabeledButton(
    "OpenWoWMobileTargetButton", utilityRow, L.enemy,
    "Interface\\Icons\\Ability_Hunter_SniperShot",
    function() TargetNearestEnemy() end)
local friendlyButton = CreateLabeledButton(
    "OpenWoWMobileFriendlyButton", utilityRow, L.friendly,
    "Interface\\Icons\\Spell_Holy_PrayerOfHealing02",
    function() TargetNearestFriend() end)
local interactButton = CreateLabeledButton(
    "OpenWoWMobileInteractButton", utilityRow, L.interact,
    "Interface\\Icons\\INV_Misc_Hand_01",
    function() InteractUnit("target") end)
local jumpButton = CreateLabeledButton(
    "OpenWoWMobileJumpButton", utilityRow, L.jump,
    "Interface\\Icons\\Ability_Rogue_Sprint",
    function() RunMobileBinding("JUMP") end)
local drawerButton = CreateLabeledButton(
    "OpenWoWMobileDrawerButton", utilityRow, L.drawer,
    "Interface\\Icons\\INV_Misc_Gear_01",
    function()
        if utilityPanel:IsShown() then
            utilityPanel:Hide()
        else
            utilityPanel:Show()
        end
    end)

local utilityDefinitions = {
    {L.character, "Interface\\Icons\\INV_Shirt_05White", "TOGGLECHARACTER0"},
    {L.spellbook, "Interface\\Icons\\INV_Misc_Book_09", "TOGGLESPELLBOOK"},
    {L.talents, "Interface\\Icons\\Spell_Nature_StoneClawTotem", "TOGGLETALENTS"},
    {L.quest, "Interface\\Icons\\INV_Misc_Note_05", "TOGGLEQUESTLOG"},
    {L.map, "Interface\\Icons\\INV_Misc_Map_01", "TOGGLEWORLDMAP"},
    {L.bags, "Interface\\Icons\\INV_Misc_Bag_08", "OPENALLBAGS"},
    {L.chat, "Interface\\Icons\\INV_Letter_15", "OPENCHAT"},
    {L.system, "Interface\\Icons\\Trade_Engineering", "TOGGLEGAMEMENU"},
}
local utilityButtons = {}
for index, definition in ipairs(utilityDefinitions) do
    local binding = definition[3]
    utilityButtons[index] = CreateLabeledButton(
        "OpenWoWMobileUtility" .. index, utilityPanel,
        definition[1], definition[2],
        function()
            utilityPanel:Hide()
            RunMobileBinding(binding)
        end)
end

local layerButton = CreateLabeledButton(
    "OpenWoWMobileLayerButton", root, "1 / 2",
    "Interface\\Icons\\INV_Misc_Rune_01",
    function()
        state.layer = state.layer == 1 and 2 or 1
        OpenWoWMobile_RefreshActions()
    end)

local targetName = root:CreateFontString(nil, "OVERLAY", "GameFontNormal")
targetName:SetJustifyH("RIGHT")
targetName:SetTextColor(1.0, 0.82, 0.45)

local joystick = CreateFrame("Frame", "OpenWoWMobileJoystick", root)
joystick:SetFrameStrata("HIGH")
joystick:Hide()
local joystickBase = joystick:CreateTexture(nil, "BACKGROUND")
joystickBase:SetAllPoints(joystick)
joystickBase:SetTexture("Interface\\Buttons\\UI-Quickslot2")
joystickBase:SetVertexColor(0.4, 0.52, 0.66, 0.62)
local joystickKnob = CreateFrame("Frame", nil, joystick)
local joystickKnobTexture = joystickKnob:CreateTexture(nil, "ARTWORK")
joystickKnobTexture:SetAllPoints(joystickKnob)
joystickKnobTexture:SetTexture("Interface\\Buttons\\UI-Quickslot2")
joystickKnobTexture:SetVertexColor(0.92, 0.76, 0.34, 0.92)

local function UpdateActionVisual(button, actionIndex)
    button.actionIndex = actionIndex
    if button.slotLabel then
        button.slotLabel:SetText(actionIndex)
    end
    local page = GetActionBarPage() or 1
    local slot = (page - 1) * 12 + actionIndex
    local texture = GetActionTexture(slot)
    if texture then
        button.icon:SetTexture(texture)
        button.icon:SetVertexColor(1, 1, 1, 1)
    else
        button.icon:SetTexture(0.035, 0.045, 0.065, 1)
    end

    local count = GetActionCount(slot) or 0
    if button.count then
        button.count:SetText(count > 1 and count or "")
    end

    local start, duration, enable = GetActionCooldown(slot)
    if enable and enable ~= 0 and duration and duration > 0 then
        button.cooldown:SetCooldown(start or 0, duration)
        button.cooldown:Show()
    else
        button.cooldown:SetCooldown(0, 0)
        button.cooldown:Hide()
    end

    local usable, noMana = IsUsableAction(slot)
    if texture and not usable then
        if noMana then
            button.icon:SetVertexColor(0.45, 0.45, 1.0, 1)
        else
            button.icon:SetVertexColor(0.45, 0.45, 0.45, 1)
        end
    end
    if IsCurrentAction(slot) then
        button.border:SetVertexColor(1.0, 0.78, 0.2, 1)
    else
        button.border:SetVertexColor(1, 1, 1, 1)
    end
end

function OpenWoWMobile_RefreshActions()
    local visibleCount = state.wide and 12 or 6
    for index = 1, 12 do
        local button = state.actionButtons[index]
        if index <= visibleCount then
            button:Show()
            local actionIndex = state.wide and index or
                ((state.layer - 1) * 6 + index)
            UpdateActionVisual(button, actionIndex)
        else
            button:Hide()
        end
    end

    if state.wide then
        layerButton:Hide()
        for index = 1, 6 do
            state.hiddenButtons[index]:Hide()
        end
    else
        layerButton:Show()
        layerButton.label:SetText(state.layer .. " / 2")
        local hiddenLayer = state.layer == 1 and 2 or 1
        for index = 1, 6 do
            local button = state.hiddenButtons[index]
            button:Show()
            UpdateActionVisual(button, (hiddenLayer - 1) * 6 + index)
        end
    end

    local target = UnitName("target")
    if target then
        targetName:SetText(target)
    else
        targetName:SetText("")
    end
end

local function LayoutButtonGrid(buttons, count, columns, buttonSize, gap,
                                parent)
    for index = 1, count do
        local button = buttons[index]
        local column = math.mod(index - 1, columns)
        local row = math.floor((index - 1) / columns)
        button:ClearAllPoints()
        button:SetSize(buttonSize, buttonSize)
        button:SetPoint("TOPLEFT", parent, "TOPLEFT",
                        column * (buttonSize + gap),
                        -row * (buttonSize + gap))
    end
end

function OpenWoWMobile_ApplyMetrics(drawableWidth, drawableHeight,
                                    logicalWidth, logicalHeight,
                                    safeLeft, safeTop, safeRight, safeBottom)
    if not logicalWidth or not logicalHeight or logicalHeight <= 0 then
        return
    end
    state.drawableWidth = drawableWidth
    state.drawableHeight = drawableHeight
    state.logicalWidth = logicalWidth
    state.logicalHeight = logicalHeight
    state.wide = logicalWidth >= 900 and logicalHeight >= 600

    local unitsPerPoint = UIParent:GetHeight() / logicalHeight
    local actionSize = 58 * unitsPerPoint
    local actionGap = 5 * unitsPerPoint
    local utilitySize = 48 * unitsPerPoint
    local utilityGap = 5 * unitsPerPoint
    local margin = 14 * unitsPerPoint
    local safeRightUnits = safeRight * unitsPerPoint
    local safeTopUnits = safeTop * unitsPerPoint
    local safeBottomUnits = safeBottom * unitsPerPoint

    local columns = state.wide and 4 or 3
    local rows = state.wide and 3 or 2
    actionCluster:ClearAllPoints()
    actionCluster:SetSize(columns * actionSize + (columns - 1) * actionGap,
                          rows * actionSize + (rows - 1) * actionGap)
    actionCluster:SetPoint("BOTTOMRIGHT", UIParent, "BOTTOMRIGHT",
                           -(safeRightUnits + margin),
                           safeBottomUnits + margin)
    LayoutButtonGrid(state.actionButtons, state.wide and 12 or 6,
                     columns, actionSize, actionGap, actionCluster)

    utilityRow:ClearAllPoints()
    utilityRow:SetSize(5 * utilitySize + 4 * utilityGap, utilitySize)
    utilityRow:SetPoint("BOTTOMRIGHT", actionCluster, "TOPRIGHT", 0,
                        (state.wide and 10 or 66) * unitsPerPoint)
    local rowButtons = {
        targetButton, friendlyButton, interactButton, jumpButton, drawerButton
    }
    LayoutButtonGrid(rowButtons, 5, 5, utilitySize, utilityGap, utilityRow)

    targetName:ClearAllPoints()
    targetName:SetPoint("BOTTOMRIGHT", utilityRow, "TOPRIGHT", 0,
                        7 * unitsPerPoint)

    utilityPanel:ClearAllPoints()
    utilityPanel:SetSize(3 * utilitySize + 4 * utilityGap,
                         3 * utilitySize + 4 * utilityGap)
    utilityPanel:SetPoint("TOPRIGHT", UIParent, "TOPRIGHT",
                          -(safeRightUnits + margin),
                          -(safeTopUnits + margin))
    for index, button in ipairs(utilityButtons) do
        local column = math.mod(index - 1, 3)
        local row = math.floor((index - 1) / 3)
        button:ClearAllPoints()
        button:SetSize(utilitySize, utilitySize)
        button:SetPoint("TOPLEFT", utilityPanel, "TOPLEFT",
                        utilityGap + column * (utilitySize + utilityGap),
                        -(utilityGap + row * (utilitySize + utilityGap)))
    end

    local hiddenSize = 26 * unitsPerPoint
    local hiddenGap = 4 * unitsPerPoint
    layerButton:ClearAllPoints()
    layerButton:SetSize(utilitySize, utilitySize)
    layerButton:SetPoint("BOTTOMRIGHT", actionCluster, "TOPRIGHT",
                         -6 * (hiddenSize + hiddenGap),
                         8 * unitsPerPoint)
    for index, button in ipairs(state.hiddenButtons) do
        button:ClearAllPoints()
        button:SetSize(hiddenSize, hiddenSize)
        button:SetPoint("BOTTOMRIGHT", actionCluster, "TOPRIGHT",
                        -(index - 1) * (hiddenSize + hiddenGap),
                        8 * unitsPerPoint)
    end

    local joystickSize = 118 * unitsPerPoint
    local knobSize = 54 * unitsPerPoint
    joystick:SetSize(joystickSize, joystickSize)
    joystickKnob:SetSize(knobSize, knobSize)

    OpenWoWMobile_RefreshActions()
end

function OpenWoWMobile_SetJoystick(originX, originY, knobX, knobY, active)
    if not active then
        joystick:Hide()
        return
    end
    if state.drawableWidth <= 0 or state.drawableHeight <= 0 then
        return
    end
    local horizontalScale = UIParent:GetWidth() / state.drawableWidth
    local verticalScale = UIParent:GetHeight() / state.drawableHeight
    local originUiX = originX * horizontalScale
    local originUiY = (state.drawableHeight - originY) * verticalScale
    local knobUiX = knobX * horizontalScale
    local knobUiY = (state.drawableHeight - knobY) * verticalScale
    joystick:ClearAllPoints()
    joystick:SetPoint("CENTER", UIParent, "BOTTOMLEFT", originUiX, originUiY)
    joystickKnob:ClearAllPoints()
    joystickKnob:SetPoint("CENTER", UIParent, "BOTTOMLEFT", knobUiX, knobUiY)
    joystick:Show()
end

root:RegisterEvent("PLAYER_ENTERING_WORLD")
root:RegisterEvent("PLAYER_TARGET_CHANGED")
root:RegisterEvent("ACTIONBAR_PAGE_CHANGED")
root:RegisterEvent("ACTIONBAR_SLOT_CHANGED")
root:RegisterEvent("ACTIONBAR_UPDATE_STATE")
root:RegisterEvent("ACTIONBAR_UPDATE_USABLE")
root:RegisterEvent("ACTIONBAR_UPDATE_COOLDOWN")
root:RegisterEvent("UPDATE_BONUS_ACTIONBAR")
root:SetScript("OnEvent", function()
    OpenWoWMobile_RefreshActions()
end)
root:SetScript("OnUpdate", function(self, elapsed)
    state.refreshElapsed = state.refreshElapsed + elapsed
    if state.refreshElapsed >= 0.10 then
        state.refreshElapsed = 0
        OpenWoWMobile_RefreshActions()
    end
end)

OpenWoWMobile_ApplyMetrics(UIParent:GetWidth(), UIParent:GetHeight(),
                           844, 390, 0, 0, 0, 0)
