#include "Garden.h"
#include "Config.h"
#include "PlantRegistry.h"

namespace
{
	constexpr std::uint32_t kRecordType = 'GRDN';
	constexpr std::uint32_t kRecordVersion = 1;

	float Now()
	{
		auto* cal = RE::Calendar::GetSingleton();
		return cal ? cal->GetDaysPassed() : 0.0f;
	}

	bool IsHarvested(RE::TESObjectREFR* a_ref)
	{
		return (a_ref->formFlags & static_cast<std::uint32_t>(RE::TESObjectREFR::RecordFlags::kHarvested)) != 0;
	}

	void Notify(const std::string& a_msg)
	{
		if (Config::showMessages) {
			RE::SendHUDMessage::ShowHUDMessage(a_msg.c_str());
		}
	}

	void RunOnMainThread(std::function<void()> a_fn)
	{
		if (auto* task = SKSE::GetTaskInterface()) {
			task->AddTask(std::move(a_fn));
		}
	}
}

// ---------------------------------------------------------------- planting

void Garden::QueueSprout(RE::FormID a_droppedRef, RE::FormID a_item, std::int32_t a_count)
{
	const auto delay = std::chrono::milliseconds(static_cast<int>(Config::sproutDelay * 1000.0f));
	std::thread([=]() {
		std::this_thread::sleep_for(delay);
		RunOnMainThread([=]() { GetSingleton()->Sprout(a_droppedRef, a_item, a_count); });
	}).detach();
}

void Garden::Sprout(RE::FormID a_droppedRef, RE::FormID a_item, std::int32_t a_count)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* item = RE::TESForm::LookupByID<RE::TESBoundObject>(a_item);
	auto* dropped = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_droppedRef);
	if (!player || !item) {
		return;
	}

	// Picked back up (or otherwise gone) before it sprouted -> do nothing.
	if (!dropped || dropped->IsDeleted() || dropped->IsDisabled() || !dropped->GetParentCell() ||
		dropped->GetBaseObject() != item) {
		spdlog::info("Dropped {:08X} no longer in the world - not planting", a_droppedRef);
		return;
	}

	auto* plantBase = PlantRegistry::GetSingleton()->PickPlant(a_item);
	if (!plantBase) {
		return;
	}

	// Use where the item actually landed, so the plant sits on the ground even on slopes.
	auto pos = dropped->GetPosition();
	pos.z -= Config::sinkDepth;

	auto plant = dropped->PlaceObjectAtMe(plantBase, true);  // persistent: survives cell resets
	if (!plant) {
		spdlog::warn("PlaceObjectAtMe failed for {:08X}", plantBase->GetFormID());
		return;
	}

	static thread_local std::mt19937       rng{ std::random_device{}() };
	std::uniform_real_distribution<float> spin(0.0f, 2.0f * std::numbers::pi_v<float>);
	plant->SetPosition(pos);
	plant->SetAngle(RE::NiPoint3(0.0f, 0.0f, spin(rng)));

	// Consume exactly one item; hand the rest of the stack back.
	dropped->Disable();
	dropped->SetDelete(true);
	if (a_count > 1) {
		player->AddObjectToContainer(item, nullptr, a_count - 1, nullptr);
	}

	{
		std::lock_guard l(_lock);
		_planted[plant->GetFormID()] = Planting{ a_item, -1.0f };
	}

	spdlog::info("Planted {} [{:08X}] as {:08X} -> ref {:08X}", item->GetName(), a_item, plantBase->GetFormID(), plant->GetFormID());
	Notify(std::format("You planted {}", item->GetName()));
}

bool Garden::IsPlanted(RE::FormID a_ref) const
{
	std::lock_guard l(_lock);
	return _planted.contains(a_ref);
}

// ---------------------------------------------------------------- harvest / regrow / uproot

void Garden::OnActivated(RE::FormID a_ref, bool a_sneakingPlayer)
{
	bool uproot = false;
	{
		std::lock_guard l(_lock);
		auto            it = _planted.find(a_ref);
		if (it == _planted.end()) {
			return;
		}
		// Already empty when activated while sneaking -> dig it up.
		uproot = Config::allowUproot && a_sneakingPlayer && it->second.harvestedDay >= 0.0f;
	}

	if (uproot) {
		RunOnMainThread([=]() { GetSingleton()->Uproot(a_ref); });
	} else {
		QueueCheck(a_ref);  // records the harvest time
	}
}

void Garden::QueueCheck(RE::FormID a_ref)
{
	RunOnMainThread([=]() { GetSingleton()->Check(a_ref); });
}

void Garden::QueueCheckAllLoaded()
{
	RunOnMainThread([]() {
		auto*                   self = GetSingleton();
		std::vector<RE::FormID> ids;
		{
			std::lock_guard l(self->_lock);
			ids.reserve(self->_planted.size());
			for (const auto& [id, p] : self->_planted) {
				ids.push_back(id);
			}
		}
		for (auto id : ids) {
			if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id); ref && ref->Is3DLoaded()) {
				self->Check(id);
			}
		}
	});
}

void Garden::Check(RE::FormID a_ref)
{
	auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_ref);

	{
		std::lock_guard l(_lock);
		auto            it = _planted.find(a_ref);
		if (it == _planted.end()) {
			return;
		}
		if (!ref || ref->IsDeleted()) {
			_planted.erase(it);
			return;
		}

		const float now = Now();
		if (!IsHarvested(ref)) {
			it->second.harvestedDay = -1.0f;
			return;
		}
		if (it->second.harvestedDay < 0.0f) {
			it->second.harvestedDay = now;
			return;
		}
		if (now - it->second.harvestedDay < Config::regrowDays) {
			return;
		}
		it->second.harvestedDay = -1.0f;
	}

	// Regrow
	ref->formFlags &= ~static_cast<std::uint32_t>(RE::TESObjectREFR::RecordFlags::kHarvested);
	ref->AddChange(RE::TESObjectREFR::ChangeFlags::kEmpty);
	if (ref->Is3DLoaded()) {
		ref->Disable();  // rebuild 3D so the produce shows again
		ref->Enable(false);
	}
	spdlog::info("Regrew {:08X}", a_ref);
}

void Garden::Uproot(RE::FormID a_ref)
{
	RE::FormID itemID = 0;
	{
		std::lock_guard l(_lock);
		auto            it = _planted.find(a_ref);
		if (it == _planted.end()) {
			return;
		}
		itemID = it->second.item;
		_planted.erase(it);
	}

	if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_ref)) {
		ref->Disable();
		ref->SetDelete(true);
	}

	auto* item = RE::TESForm::LookupByID<RE::TESBoundObject>(itemID);
	if (auto* player = RE::PlayerCharacter::GetSingleton(); player && item) {
		player->AddObjectToContainer(item, nullptr, 1, nullptr);
		Notify(std::format("You uprooted {}", item->GetName()));
	}
	spdlog::info("Uprooted {:08X}", a_ref);
}

// ---------------------------------------------------------------- co-save

void Garden::Save(SKSE::SerializationInterface* a_intfc)
{
	std::lock_guard l(_lock);
	if (!a_intfc->OpenRecord(kRecordType, kRecordVersion)) {
		spdlog::error("Failed to open co-save record");
		return;
	}
	const auto count = static_cast<std::uint32_t>(_planted.size());
	a_intfc->WriteRecordData(&count, sizeof(count));
	for (const auto& [id, p] : _planted) {
		a_intfc->WriteRecordData(&id, sizeof(id));
		a_intfc->WriteRecordData(&p.item, sizeof(p.item));
		a_intfc->WriteRecordData(&p.harvestedDay, sizeof(p.harvestedDay));
	}
}

void Garden::Load(SKSE::SerializationInterface* a_intfc)
{
	std::lock_guard l(_lock);
	_planted.clear();

	std::uint32_t type = 0, version = 0, length = 0;
	while (a_intfc->GetNextRecordInfo(type, version, length)) {
		if (type != kRecordType || version != kRecordVersion) {
			continue;
		}
		std::uint32_t count = 0;
		if (a_intfc->ReadRecordData(&count, sizeof(count)) != sizeof(count)) {
			break;
		}
		for (std::uint32_t i = 0; i < count; ++i) {
			RE::FormID id = 0, item = 0;
			float      day = -1.0f;
			if (a_intfc->ReadRecordData(&id, sizeof(id)) != sizeof(id) ||
				a_intfc->ReadRecordData(&item, sizeof(item)) != sizeof(item) ||
				a_intfc->ReadRecordData(&day, sizeof(day)) != sizeof(day)) {
				break;
			}
			RE::FormID newID = 0, newItem = 0;
			if (a_intfc->ResolveFormID(id, newID) && a_intfc->ResolveFormID(item, newItem)) {
				_planted[newID] = Planting{ newItem, day };
			}
		}
	}
	spdlog::info("Loaded {} planted plants from co-save", _planted.size());
}

void Garden::Revert()
{
	std::lock_guard l(_lock);
	_planted.clear();
}
