#pragma once

// Tracks every plant the player has planted, handles sprouting, regrowth and uprooting,
// and saves the list in the SKSE co-save.
class Garden
{
public:
	static Garden* GetSingleton()
	{
		static Garden singleton;
		return &singleton;
	}

	// Called from the drop event; waits for the item to land, then turns it into a plant.
	void QueueSprout(RE::FormID a_droppedRef, RE::FormID a_item, std::int32_t a_count);

	[[nodiscard]] bool IsPlanted(RE::FormID a_ref) const;

	void OnActivated(RE::FormID a_ref, bool a_sneakingPlayer);
	void QueueCheck(RE::FormID a_ref);
	void QueueCheckAllLoaded();

	// Serialization
	void Save(SKSE::SerializationInterface* a_intfc);
	void Load(SKSE::SerializationInterface* a_intfc);
	void Revert();

private:
	struct Planting
	{
		RE::FormID item{ 0 };           // what was planted (returned when uprooted)
		float      harvestedDay{ -1.0f };  // game day the harvest was noticed, -1 = has produce
	};

	void Sprout(RE::FormID a_droppedRef, RE::FormID a_item, std::int32_t a_count);
	void Check(RE::FormID a_ref);
	void Uproot(RE::FormID a_ref);

	mutable std::mutex                       _lock;
	std::unordered_map<RE::FormID, Planting> _planted;
};
