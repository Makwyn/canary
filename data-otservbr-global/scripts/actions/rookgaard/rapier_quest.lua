local rapierQuest = Action()
local reward = 3272

function rapierQuest.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local itemType = ItemType(reward)
	local itemWeight = itemType:getWeight()
	if player:getFreeCapacity() >= itemType:getWeight() then
		if not player:questKV("rapier"):get("completed") then
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You have found a rapier.")
			player:addItem(reward, 1)
			player:questKV("rapier"):set("completed", true)
		else
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The box is empty.")
		end
	else
		player:sendTextMessage(MESSAGE_LOOK, "You have found a " .. itemType:getName() .. " weighing " .. (itemWeight / 100) .. " oz, but it's too heavy.")
	end
	return true
end

rapierQuest:uid(14042)
rapierQuest:register()
