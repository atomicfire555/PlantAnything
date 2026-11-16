#include "Events.h"
#include "Config.h"
#include "Garden.h"
#include "PlantRegistry.h"

namespace
{
	using Control = RE::BSEventNotifyControl;

	class Sink final :
		public RE::BSTEventSink<RE::TESContainerChangedEvent>,
		public RE::BSTEventSink<RE::TESActivateEvent>,
		public RE::BSTEventSink<RE::TESCellAttachDetachEvent>,
		public RE::BSTEventSink<RE::TESWaitStopEvent>,
		public RE::BSTEventSink<RE::TESSleepStopEvent>
	{
	public:
		static Sink* GetSingleton()
		{
			static Sink singleton;
			return &singleton;
		}

		// Player drops a plantable item while sneaking -> plant it.
		Control ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
		{
			if (!a_event || a_event->newContainer != 0 || !a_event->reference || a_event->itemCount <= 0) {
				return Control::kContinue;  // not a drop into the world
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_event->oldContainer != player->GetFormID()) {
				return Control::kContinue;
			}
			if (!PlantRegistry::GetSingleton()->IsPlantable(a_event->baseObj)) {
				return Control::kContinue;
			}
			if (Config::requireSneak && !player->IsSneaking()) {
				return Control::kContinue;
			}
			if (!Config::allowInteriors) {
				if (auto* cell = player->GetParentCell(); cell && cell->IsInteriorCell()) {
					if (Config::showMessages) {
						RE::SendHUDMessage::ShowHUDMessage("You can only plant outdoors");
					}
					return Control::kContinue;
				}
			}
			const auto dropped = a_event->reference.get();
			if (!dropped) {
				return Control::kContinue;
			}
			Garden::GetSingleton()->QueueSprout(dropped->GetFormID(), a_event->baseObj, a_event->itemCount);
			return Control::kContinue;
		}

		// Harvest tracking + sneak-activate to uproot.
		Control ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
		{
			if (!a_event || !a_event->objectActivated) {
				return Control::kContinue;
			}
			const auto id = a_event->objectActivated->GetFormID();
			if (!Garden::GetSingleton()->IsPlanted(id)) {
				return Control::kContinue;
			}
			auto*      player = RE::PlayerCharacter::GetSingleton();
			const bool byPlayer = player && a_event->actionRef && a_event->actionRef.get() == player;
			const bool sneaking = byPlayer && player->IsSneaking();
			Garden::GetSingleton()->OnActivated(id, sneaking);
			return Control::kContinue;
		}

		// A planted plant comes into view -> regrow if enough time has passed.
		Control ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
		{
			if (a_event && a_event->attached && a_event->reference) {
				const auto id = a_event->reference->GetFormID();
				if (Garden::GetSingleton()->IsPlanted(id)) {
					Garden::GetSingleton()->QueueCheck(id);
				}
			}
			return Control::kContinue;
		}

		// Waiting / sleeping next to your garden also regrows it.
		Control ProcessEvent(const RE::TESWaitStopEvent*, RE::BSTEventSource<RE::TESWaitStopEvent>*) override
		{
			Garden::GetSingleton()->QueueCheckAllLoaded();
			return Control::kContinue;
		}

		Control ProcessEvent(const RE::TESSleepStopEvent*, RE::BSTEventSource<RE::TESSleepStopEvent>*) override
		{
			Garden::GetSingleton()->QueueCheckAllLoaded();
			return Control::kContinue;
		}
	};
}

void Events::Register()
{
	auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
	if (!holder) {
		spdlog::error("ScriptEventSourceHolder unavailable");
		return;
	}
	auto* sink = Sink::GetSingleton();
	holder->AddEventSink<RE::TESContainerChangedEvent>(sink);
	holder->AddEventSink<RE::TESActivateEvent>(sink);
	holder->AddEventSink<RE::TESCellAttachDetachEvent>(sink);
	holder->AddEventSink<RE::TESWaitStopEvent>(sink);
	holder->AddEventSink<RE::TESSleepStopEvent>(sink);
	spdlog::info("Event sinks registered");
}
