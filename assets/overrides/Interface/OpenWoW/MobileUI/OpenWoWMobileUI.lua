local state = {
    layer = 1,
    drawableWidth = 1,
    drawableHeight = 1,
    logicalWidth = 844,
    logicalHeight = 390,
    actionButtons = {},
}

RegisterForSave("OpenWoWMobileHUDShown")

local locale = GetLocale and GetLocale() or "enUS"
local L
if locale == "zhCN" then
    L = {
        enemy = "敌人",
        friendly = "友方",
        interact = "交互",
        interactHint = "此按钮与当前目标交互。采集物、宝箱等无需选中，直接双击场景中的物体。",
        jump = "跳跃",
        drawer = "功能",
        close = "返回",
        hide = "收起",
        show = "触控",
        hideHint = "隐藏触控 HUD，露出原生界面",
        showHint = "显示触控 HUD",
        gestureHint = "隐藏时摇杆停止并停用；镜头与世界点击手势仍可使用。",
        move = "移动",
        character = "人物",
        spellbook = "法术",
        talents = "天赋",
        quest = "任务",
        map = "地图",
        bags = "背包",
        chat = "聊天",
        system = "系统",
        primary = "点击",
        secondary = "右键",
        dismiss = "关闭",
        inspectHint = "查看不会执行操作",
        dragHint = "长按后移动可拖动",
    }
else
    L = {
        enemy = "Enemy",
        friendly = "Friend",
        interact = "Use",
        interactHint = "This button interacts with your current target. Double-tap a gathering object or chest in the world to use it without selecting it.",
        jump = "Jump",
        drawer = "Menu",
        close = "Back",
        hide = "Hide",
        show = "Touch",
        hideHint = "Hide the touch HUD to access the original UI",
        showHint = "Show the touch HUD",
        gestureHint = "Hiding stops and disables the stick. Camera and world-tap gestures remain available.",
        move = "Move",
        character = "Hero",
        spellbook = "Spells",
        talents = "Talents",
        quest = "Quests",
        map = "Map",
        bags = "Bags",
        chat = "Chat",
        system = "System",
        primary = "Click",
        secondary = "Right click",
        dismiss = "Close",
        inspectHint = "Inspect without activating",
        dragHint = "Hold, then move to drag",
    }
end

local root = CreateFrame("Frame", "OpenWoWMobileRoot", UIParent)
root:SetAllPoints(UIParent)
root:SetFrameStrata("MEDIUM")

local actionCluster = CreateFrame("Frame", "OpenWoWMobileActionCluster", root)

local function AddPanelBackground(frame, alpha)
    local background = frame:CreateTexture(nil, "BACKGROUND")
    background:SetAllPoints(frame)
    background:SetTexture(0.015, 0.02, 0.03, alpha or 0.72)
    frame.background = background
end

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

for index = 1, 6 do
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
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetAction(((GetActionBarPage() or 1) - 1) * 12 + self.actionIndex)
        GameTooltip:Show()
    end)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    state.actionButtons[index] = button
end

local utilityPanel = CreateFrame("Frame", "OpenWoWMobileUtilityPanel", root)
utilityPanel:EnableMouse(true)
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
    "OpenWoWMobileTargetButton", actionCluster, L.enemy,
    "Interface\\Icons\\Ability_Hunter_SniperShot",
    function() TargetNearestEnemy() end)
local friendlyButton = CreateLabeledButton(
    "OpenWoWMobileFriendlyButton", actionCluster, L.friendly,
    "Interface\\Icons\\Spell_Holy_PrayerOfHealing02",
    function() TargetNearestFriend() end)
local interactButton = CreateLabeledButton(
    "OpenWoWMobileInteractButton", actionCluster, L.interact,
    "Interface\\Icons\\INV_Misc_Hand_01",
    function() InteractUnit("target") end)
interactButton:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:SetText(L.interact)
    GameTooltip:AddLine(L.interactHint, 1, 1, 1, true)
    GameTooltip:Show()
end)
interactButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
local jumpButton = CreateLabeledButton(
    "OpenWoWMobileJumpButton", actionCluster, L.jump,
    "Interface\\Icons\\Ability_Rogue_Sprint",
    function() RunMobileBinding("JUMP") end)
local drawerButton
local function SetDrawerShown(shown)
    if shown then
        actionCluster:Hide()
        utilityPanel:Show()
        drawerButton.label:SetText(L.close)
    else
        utilityPanel:Hide()
        actionCluster:Show()
        drawerButton.label:SetText(L.drawer)
    end
end

drawerButton = CreateLabeledButton(
    "OpenWoWMobileDrawerButton", root, L.drawer,
    "Interface\\Icons\\INV_Misc_Gear_01",
    function()
        SetDrawerShown(not utilityPanel:IsShown())
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
            SetDrawerShown(false)
            RunMobileBinding(binding)
        end)
end

local layerButton = CreateLabeledButton(
    "OpenWoWMobileLayerButton", actionCluster, "1–6",
    "Interface\\Icons\\INV_Misc_Rune_01",
    function()
        state.layer = state.layer == 1 and 2 or 1
        OpenWoWMobile_RefreshActions()
    end)

-- The launcher must outlive the hidden HUD and a hidden minimap. Anchor to the
-- map when it is visible, but keep ownership with UIParent.
local toggleButton = CreateLabeledButton(
    "OpenWoWMobileToggleButton", UIParent, L.hide,
    "Interface\\Icons\\INV_Misc_EngGizmos_19",
    function()
        OpenWoWMobileHUDShown = not root:IsShown()
        OpenWoWMobile_ApplyVisibility()
        GameTooltip:Hide()
    end)
toggleButton:SetFrameStrata("MEDIUM")
toggleButton:SetFrameLevel(Minimap:GetFrameLevel() + 8)
toggleButton:SetClampedToScreen(true)
toggleButton:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:SetText(root:IsShown() and L.hideHint or L.showHint)
    GameTooltip:AddLine(L.gestureHint, 1, 1, 1, true)
    GameTooltip:Show()
end)
toggleButton:SetScript("OnLeave", function() GameTooltip:Hide() end)

function OpenWoWMobile_ApplyVisibility()
    SetDrawerShown(false)
    if OpenWoWMobileHUDShown == false then
        root:Hide()
        toggleButton.label:SetText(L.show)
        toggleButton.border:SetVertexColor(0.55, 0.7, 0.9, 1)
    else
        root:Show()
        toggleButton.label:SetText(L.hide)
        toggleButton.border:SetVertexColor(1, 0.78, 0.2, 1)
        OpenWoWMobile_RefreshActions()
    end
end

local joystick = CreateFrame("Frame", "OpenWoWMobileJoystick", root)
-- Explicit opt-in consumed by the native touch router. The normal frame hit
-- determines ownership, including visibility, layout and occluding panels.
joystick.__ow_touch_movement = true
joystick:EnableMouse(true)
local joystickBase = joystick:CreateTexture(nil, "BACKGROUND")
joystickBase:SetAllPoints(joystick)
joystickBase:SetTexture("Interface\\Buttons\\UI-Quickslot2")
joystickBase:SetVertexColor(0.4, 0.52, 0.66, 0.62)
local joystickKnob = CreateFrame("Frame", nil, joystick)
joystickKnob:SetPoint("CENTER", joystick, "CENTER", 0, 0)
local joystickKnobTexture = joystickKnob:CreateTexture(nil, "ARTWORK")
joystickKnobTexture:SetAllPoints(joystickKnob)
joystickKnobTexture:SetTexture("Interface\\Buttons\\UI-Quickslot2")
joystickKnobTexture:SetVertexColor(0.92, 0.76, 0.34, 0.92)
local joystickLabel = joystick:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
joystickLabel:SetPoint("BOTTOM", joystick, "BOTTOM", 0, 8)
joystickLabel:SetText(L.move)

local function UpdateActionVisual(button, actionIndex)
    button.actionIndex = actionIndex
    if button.slotLabel and button.visualActionIndex ~= actionIndex then
        button.slotLabel:SetText(actionIndex)
    end
    button.visualActionIndex = actionIndex

    local page = GetActionBarPage() or 1
    local slot = (page - 1) * 12 + actionIndex
    local texture = GetActionTexture(slot)
    if not button.visualTextureInitialized or button.visualTexture ~= texture then
        if texture then
            button.icon:SetTexture(texture)
        else
            button.icon:SetTexture(0.035, 0.045, 0.065, 1)
        end
        button.visualTexture = texture
        button.visualTextureInitialized = true
    end

    local count = GetActionCount(slot) or 0
    local countText = count > 1 and count or ""
    if button.count and button.visualCount ~= countText then
        button.count:SetText(countText)
        button.visualCount = countText
    end

    local start, duration, enable = GetActionCooldown(slot)
    local cooldownActive =
        (enable and enable ~= 0 and duration and duration > 0) and true or false
    start = cooldownActive and (start or 0) or 0
    duration = cooldownActive and duration or 0
    if button.visualCooldownStart ~= start or
            button.visualCooldownDuration ~= duration then
        button.cooldown:SetCooldown(start, duration)
        button.visualCooldownStart = start
        button.visualCooldownDuration = duration
    end
    if button.visualCooldownActive ~= cooldownActive then
        if cooldownActive then
            button.cooldown:Show()
        else
            button.cooldown:Hide()
        end
        button.visualCooldownActive = cooldownActive
    end

    local usable, noMana = IsUsableAction(slot)
    local iconColor
    if texture and not usable then
        if noMana then
            iconColor = "mana"
        else
            iconColor = "disabled"
        end
    else
        iconColor = "normal"
    end
    if button.visualIconColor ~= iconColor then
        if iconColor == "mana" then
            button.icon:SetVertexColor(0.45, 0.45, 1.0, 1)
        elseif iconColor == "disabled" then
            button.icon:SetVertexColor(0.45, 0.45, 0.45, 1)
        else
            button.icon:SetVertexColor(1, 1, 1, 1)
        end
        button.visualIconColor = iconColor
    end

    local current = IsCurrentAction(slot) and true or false
    if button.visualCurrent ~= current then
        if current then
            button.border:SetVertexColor(1.0, 0.78, 0.2, 1)
        else
            button.border:SetVertexColor(1, 1, 1, 1)
        end
        button.visualCurrent = current
    end
end

function OpenWoWMobile_RefreshActions()
    for index, button in ipairs(state.actionButtons) do
        UpdateActionVisual(button, (state.layer - 1) * 6 + index)
    end
    layerButton.label:SetText(state.layer == 1 and "1–6" or "7–12")
end

local function SizeButton(button, size, unitsPerPoint)
    button:SetSize(size * unitsPerPoint, size * unitsPerPoint)
    for _, field in ipairs({"label", "slotLabel", "count"}) do
        local text = button[field]
        if text then
            local font, _, flags = text:GetFont()
            text:SetFont(font, (field == "count" and 12 or 11) * unitsPerPoint,
                         flags)
        end
    end
    if button.label then
        button.label:ClearAllPoints()
        button.label:SetPoint("BOTTOM", button, "BOTTOM", 0, 6 * unitsPerPoint)
        button.label:SetWidth((size - 8) * unitsPerPoint)
    end
end

local function PlaceToggle()
    local u = state.unitsPerPoint
    local bounds = state.safeBounds
    local size = 48 * u
    local right = bounds.right
    local top = bounds.top
    if Minimap:IsVisible() then
        local scale = Minimap:GetEffectiveScale() / UIParent:GetEffectiveScale()
        right = Minimap:GetLeft() * scale - UIParent:GetLeft() - 8 * u
        top = (Minimap:GetTop() + Minimap:GetBottom()) * 0.5 * scale -
              UIParent:GetBottom() + size * 0.5
    end
    -- Stay inside the device safe area even if the map is moved or rescaled.
    right = math.max(bounds.left + size, math.min(bounds.right, right))
    top = math.max(bounds.bottom + size, math.min(bounds.top, top))
    toggleButton:ClearAllPoints()
    toggleButton:SetPoint("TOPRIGHT", UIParent, "BOTTOMLEFT", right, top)
end

-- Inspection is independent of the combat HUD's visibility. The native input
-- router owns the inspected target and validates it again before dispatch.
local touchContext = CreateFrame("Frame", "OpenWoWMobileTouchContext", UIParent)
touchContext:SetFrameStrata("DIALOG")
touchContext:EnableMouse(true)
touchContext:SetClampedToScreen(true)
AddPanelBackground(touchContext, 0.95)
touchContext:Hide()
local contextHint = touchContext:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
contextHint:SetPoint("TOP", touchContext, "TOP", 0, -8)
local contextAction
local contextButtons = {}
for index, definition in ipairs({
    {L.primary, 1, "Interface\\Icons\\INV_Misc_Hand_01"},
    {L.secondary, 4, "Interface\\Icons\\INV_Misc_Gear_01"},
    {L.dismiss, 0, "Interface\\Buttons\\UI-StopButton"},
}) do
    local action = definition[2]
    contextButtons[index] = CreateLabeledButton(
        "OpenWoWMobileTouchContext" .. index, touchContext,
        definition[1], definition[3], function()
            if not contextAction then error("Mobile touch context has expired") end
            contextAction(action)
        end)
end

function OpenWoWMobile_HideTouchContext()
    contextAction = nil
    touchContext:Hide()
end

function OpenWoWMobile_ShowTouchContext(x, y, secondary, draggable, callback)
    local u = state.unitsPerPoint
    local unitsPerPixel = GetScreenHeight() / state.drawableHeight
    local originX = x * unitsPerPixel - UIParent:GetLeft()
    local originY = (state.drawableHeight - y) * unitsPerPixel - UIParent:GetBottom()
    local width, height = 240 * u, 96 * u
    local left = math.max(8 * u, math.min(UIParent:GetWidth() - width - 8 * u,
                                         originX - width * 0.5))
    local bottom = originY + 20 * u
    if bottom + height > UIParent:GetHeight() - 8 * u then
        bottom = originY - height - 20 * u
    end
    bottom = math.max(8 * u, bottom)
    touchContext:SetSize(width, height)
    touchContext:ClearAllPoints()
    touchContext:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", left, bottom)
    local widths = {72, 80, 56}
    local offset = 8
    for index, button in ipairs(contextButtons) do
        SizeButton(button, 64, u)
        button:SetWidth(widths[index] * u)
        button.label:SetWidth((widths[index] - 8) * u)
        button:ClearAllPoints()
        button:SetPoint("BOTTOMLEFT", touchContext, "BOTTOMLEFT", offset * u, 8 * u)
        offset = offset + widths[index] + 8
    end
    local font, _, flags = contextHint:GetFont()
    contextHint:SetFont(font, 11 * u, flags)
    contextHint:ClearAllPoints()
    contextHint:SetPoint("TOP", touchContext, "TOP", 0, -8 * u)
    contextHint:SetText(draggable and L.dragHint or L.inspectHint)
    if secondary then contextButtons[2]:Enable() else contextButtons[2]:Disable() end
    contextAction = callback
    touchContext:Show()
    return touchContext:GetName()
end

-- Centers in physical points from the lower-right safe-area margin. The
-- primary slot sits at the thumb's rest position; the other five follow its
-- sweep. Both layers and both device classes keep the same muscle memory.
local actionLayout = {
    {88, 72, 68},
    {160, 52, 56},
    {226, 82, 56},
    {224, 148, 56},
    {158, 192, 56},
    {92, 208, 56},
}

function OpenWoWMobile_ApplyMetrics(drawableWidth, drawableHeight,
                                    logicalWidth, logicalHeight)
    if not logicalWidth or not logicalHeight or logicalWidth <= 0 or
            logicalHeight <= 0 or drawableWidth <= 0 or drawableHeight <= 0 then
        error("OpenWoWMobile_ApplyMetrics: invalid drawable or logical viewport")
    end
    state.drawableWidth = drawableWidth
    state.drawableHeight = drawableHeight
    state.logicalWidth = logicalWidth
    state.logicalHeight = logicalHeight

    -- UIParent is already inset by the native layout viewport. Size controls
    -- in device points using the full screen, then anchor inside that root.
    local u = GetScreenHeight() / logicalHeight
    state.unitsPerPoint = u
    state.safeBounds = {
        left = 14 * u,
        right = UIParent:GetWidth() - 14 * u,
        bottom = 14 * u,
        top = UIParent:GetHeight() - 14 * u,
    }

    actionCluster:ClearAllPoints()
    actionCluster:SetSize(316 * u, 236 * u)
    actionCluster:SetPoint("BOTTOMRIGHT", UIParent, "BOTTOMRIGHT",
                           -14 * u, 14 * u)

    local function PlaceControl(button, x, y, size)
        SizeButton(button, size, u)
        button:ClearAllPoints()
        button:SetPoint("CENTER", actionCluster, "BOTTOMRIGHT", -x * u, y * u)
    end
    for index, position in ipairs(actionLayout) do
        PlaceControl(state.actionButtons[index], unpack(position))
    end
    PlaceControl(jumpButton, 28, 142, 56)
    PlaceControl(interactButton, 28, 210, 52)
    PlaceControl(targetButton, 290, 142, 52)
    PlaceControl(friendlyButton, 290, 208, 52)
    PlaceControl(layerButton, 158, 128, 48)
    PlaceControl(drawerButton, 290, 72, 52)

    -- The drawer replaces the combat fan instead of adding another overlay.
    local utilitySize = 64 * u
    local utilityGap = 8 * u
    utilityPanel:ClearAllPoints()
    utilityPanel:SetSize(3 * utilitySize + 4 * utilityGap,
                         3 * utilitySize + 4 * utilityGap)
    utilityPanel:SetPoint("BOTTOMRIGHT", actionCluster, "BOTTOMRIGHT", 0, 0)
    for index, button in ipairs(utilityButtons) do
        local column = math.mod(index - 1, 3)
        local row = math.floor((index - 1) / 3)
        SizeButton(button, 64, u)
        button:ClearAllPoints()
        button:SetPoint("TOPLEFT", utilityPanel, "TOPLEFT",
                        utilityGap + column * (utilitySize + utilityGap),
                        -(utilityGap + row * (utilitySize + utilityGap)))
    end

    SizeButton(toggleButton, 48, u)
    PlaceToggle()
    joystick:SetSize(118 * u, 118 * u)
    joystick:ClearAllPoints()
    joystick:SetPoint("BOTTOMLEFT", root, "BOTTOMLEFT", 32 * u, 32 * u)
    joystickKnob:SetSize(54 * u, 54 * u)
    local font, _, flags = joystickLabel:GetFont()
    joystickLabel:SetFont(font, 11 * u, flags)
    joystickLabel:ClearAllPoints()
    joystickLabel:SetPoint("BOTTOM", joystick, "BOTTOM", 0, 8 * u)
    OpenWoWMobile_RefreshActions()
end

function OpenWoWMobile_SetJoystick(directionX, directionY, active)
    local travel = (118 - 54) * 0.5 * (state.unitsPerPoint or 1)
    joystickKnob:ClearAllPoints()
    joystickKnob:SetPoint("CENTER", joystick, "CENTER",
        active and directionX * travel or 0,
        active and -directionY * travel or 0)
    joystickBase:SetVertexColor(0.4, 0.52, 0.66, active and 0.9 or 0.62)
end

root:RegisterEvent("PLAYER_ENTERING_WORLD")
root:RegisterEvent("ACTIONBAR_PAGE_CHANGED")
root:RegisterEvent("ACTIONBAR_SLOT_CHANGED")
root:RegisterEvent("ACTIONBAR_UPDATE_STATE")
root:RegisterEvent("ACTIONBAR_UPDATE_USABLE")
root:RegisterEvent("ACTIONBAR_UPDATE_COOLDOWN")
root:RegisterEvent("UPDATE_BONUS_ACTIONBAR")
root:SetScript("OnEvent", function(self, event)
    if event == "PLAYER_ENTERING_WORLD" then
        -- Account saved variables are restored after this internal TOC loads.
        OpenWoWMobile_ApplyVisibility()
        PlaceToggle()
    else
        OpenWoWMobile_RefreshActions()
    end
end)

OpenWoWMobile_ApplyMetrics(UIParent:GetWidth(), UIParent:GetHeight(),
                           844, 390)

Minimap:HookScript("OnShow", PlaceToggle)
Minimap:HookScript("OnHide", PlaceToggle)
Minimap:HookScript("OnSizeChanged", PlaceToggle)
