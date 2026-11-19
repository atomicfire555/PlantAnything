#include "Fixups.h"
#include "Config.h"
#include "SoilPlants.h"

namespace
{
	constexpr std::uint32_t kVersion = 1;

	std::mutex                     g_lock;
	std::unordered_set<RE::FormID> g_lifted;  // so a plant is only lifted once

	bool IsBuriedHangingPlant(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref || (a_ref->GetFormID() >> 24) != 0xFF) {
			return false;  // only plants spawned at runtime (soil / drop planting)
		}
		auto* base = a_ref->GetBaseObject();
		if (!base || base->GetFormType() != RE::FormType::Flora || SoilPlants::IsGoodSoilPlant(base)) {
			return false;
		}
		const auto& b = base->boundData;
		return b.boundMax.z <= 2 && b.boundMin.z < 0;  // hangs below its origin
	}

	class Sink final : public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
	{
	public:
		static Sink* GetSingleton()
		{
			static Sink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
		{
			if (!a_event || !a_event->attached || !a_event->reference) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto* ref = a_event->reference.get();
			if (!IsBuriedHangingPlant(ref)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const auto id = ref->GetFormID();
			{
				std::lock_guard l(g_lock);
				if (!g_lifted.insert(id).second) {
					return RE::BSEventNotifyControl::kContinue;
				}
			}
			SKSE::GetTaskInterface()->AddTask([id]() {
				auto* r = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
				auto* base = r ? r->GetBaseObject() : nullptr;
				if (!base) {
					return;
				}
				auto pos = r->GetPosition();
				pos.z += static_cast<float>(-base->boundData.boundMin.z);
				r->SetPosition(pos);
				spdlog::info("Lifted buried plant {} [{:08X}] so it can be harvested", base->GetName(), id);
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

void Fixups::Register()
{
	if (!Config::liftBuriedPlants) {
		return;
	}
	if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
		holder->AddEventSink<RE::TESCellAttachDetachEvent>(Sink::GetSingleton());
	}
}

void Fixups::Save(SKSE::SerializationInterface* a_intfc)
{
	std::lock_guard l(g_lock);
	if (!a_intfc->OpenRecord(kRecordType, kVersion)) {
		return;
	}
	const auto count = static_cast<std::uint32_t>(g_lifted.size());
	a_intfc->WriteRecordData(&count, sizeof(count));
	for (const auto id : g_lifted) {
		a_intfc->WriteRecordData(&id, sizeof(id));
	}
}

void Fixups::LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
{
	std::lock_guard l(g_lock);
	g_lifted.clear();
	if (a_version != kVersion) {
		return;
	}
	std::uint32_t count = 0;
	if (a_intfc->ReadRecordData(&count, sizeof(count)) != sizeof(count)) {
		return;
	}
	for (std::uint32_t i = 0; i < count; ++i) {
		RE::FormID id = 0, newID = 0;
		if (a_intfc->ReadRecordData(&id, sizeof(id)) != sizeof(id)) {
			break;
		}
		if (a_intfc->ResolveFormID(id, newID)) {
			g_lifted.insert(newID);
		}
	}
}

void Fixups::Revert()
{
	std::lock_guard l(g_lock);
	g_lifted.clear();
}
