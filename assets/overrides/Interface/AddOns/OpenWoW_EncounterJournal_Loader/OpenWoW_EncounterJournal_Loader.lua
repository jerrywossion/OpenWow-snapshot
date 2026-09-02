BINDING_HEADER_OPENWOW_ENCOUNTER_JOURNAL = "OpenWoW 地下城手册"
BINDING_NAME_OPENWOW_TOGGLE_ENCOUNTER_JOURNAL = "打开/关闭地下城手册"

local ADDON_NAME = "OpenWoW_EncounterJournal"

local function ReportLoadFailure(reason)
    local message = "|cffff5050OpenWoW 地下城手册加载失败|r"
    if reason and reason ~= "" then
        message = message .. "：" .. tostring(reason)
    end
    if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
        DEFAULT_CHAT_FRAME:AddMessage(message)
    end
    if UIErrorsFrame and UIErrorsFrame.AddMessage then
        UIErrorsFrame:AddMessage("地下城手册加载失败", 1.0, 0.2, 0.2, 1.0)
    end
end

function OpenWoWEncounterJournalLoader_Toggle()
    if not IsAddOnLoaded(ADDON_NAME) then
        local loaded, reason = LoadAddOn(ADDON_NAME)
        if not loaded then
            ReportLoadFailure(reason)
            return
        end
    end

    if type(OpenWoWEncounterJournal_Toggle) ~= "function" then
        ReportLoadFailure("入口函数未注册")
        return
    end
    OpenWoWEncounterJournal_Toggle()
end

SLASH_OPENWOWENCOUNTERJOURNAL1 = "/ej"
SLASH_OPENWOWENCOUNTERJOURNAL2 = "/journal"
SlashCmdList.OPENWOWENCOUNTERJOURNAL = OpenWoWEncounterJournalLoader_Toggle

local launcher = CreateFrame("Button", "OpenWoWEncounterJournalLauncher", UIParent,
    "UIPanelButtonTemplate")
launcher:SetSize(92, 22)
launcher:SetPoint("BOTTOMRIGHT", UIParent, "BOTTOMRIGHT", -128, 32)
launcher:SetText("地下城手册")
launcher:SetFrameStrata("MEDIUM")
launcher:SetScript("OnClick", OpenWoWEncounterJournalLoader_Toggle)
launcher:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_TOP")
    GameTooltip:SetText("OpenWoW 地下城手册", 1.0, 0.82, 0.0)
    GameTooltip:AddLine("浏览 3.3.5a 副本、首领与可用技能资料。", 1.0, 1.0, 1.0, true)
    GameTooltip:AddLine("也可使用 /ej，或在按键设置中绑定快捷键。", 0.65, 0.65, 0.65, true)
    GameTooltip:Show()
end)
launcher:SetScript("OnLeave", function()
    GameTooltip:Hide()
end)
